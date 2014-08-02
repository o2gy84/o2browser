#include <boost/predef.h>
#include <boost/optional.hpp>
#include <boost/bind.hpp>

#include <thread>
#include <string>

#include "client.h"
#include "util.h"


TcpClient::TcpClient(unsigned timeout):
    m_Ctx(boost::asio::ssl::context::sslv23_client),
    m_IoService(new boost::asio::io_service()),
    m_Stream(*m_IoService, m_Ctx),
    m_Timeout(timeout),
    m_IsSSL(false),
    m_IsOpen(false),
    m_OwnIO(true),
    m_ExternalStrand(),
    m_Delimiter("\r\n")
{
    m_Stream.set_verify_mode(boost::asio::ssl::verify_none);
#if defined(BOOST_OS_WINDOWS)

#else
    ::fcntl(m_Stream.lowest_layer().native(), F_SETFD, FD_CLOEXEC);
#endif
}


TcpClient::TcpClient(boost::asio::io_service& _io, boost::asio::io_service::strand *strand, unsigned timeout):
    m_Ctx(boost::asio::ssl::context::sslv23_client),
    m_IoService(new boost::asio::io_service()),
    m_Stream(_io, m_Ctx),
    m_Timeout(timeout),
    m_IsSSL(false),
    m_OwnIO(false),
    m_IsOpen(false),
    m_ExternalStrand(strand),
    m_Delimiter("\r\n")
{
    m_Stream.set_verify_mode(boost::asio::ssl::verify_none);
#if defined(BOOST_OS_WINDOWS)

#else
    ::fcntl(m_Stream.lowest_layer().native(), F_SETFD, FD_CLOEXEC);
#endif
}

boost::asio::ip::tcp::resolver::iterator TcpClient::resolve(const std::string& host, const std::string& port)
{
    boost::asio::ip::tcp::resolver resolver(*m_IoService);

    //boost::asio::ip::tcp::resolver::query query( host, port);                             // Ipv6 too
    boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), host, port);    // IPv4 only
    boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);

    std::vector<boost::asio::ip::tcp::resolver::iterator> domain_names;

    for ( ; iterator != boost::asio::ip::tcp::resolver::iterator(); ++iterator)
        domain_names.push_back(iterator);

    int r = rand() % domain_names.size();
    return domain_names[r];
}


void TcpClient::connect(const std::string& host, const std::string& port) throw (std::exception)
{
    boost::asio::ip::tcp::resolver::iterator iterator = resolve(host, port);
    boost::system::error_code ec;
    boost::asio::connect(m_Stream.lowest_layer(), iterator, ec);
    if(ec) throw std::runtime_error("connect failed");
    m_IsOpen = true;
}

void TcpClient::handshake() throw(std::exception)
{
    if(ssl()) {
        boost::system::error_code ec;
        m_Stream.handshake(boost::asio::ssl::stream_base::client, ec);
        if(ec) throw std::runtime_error("handshake failed");
    }
}


static void read_completed(const boost::system::error_code& error, size_t transferred, size_t &bytes, boost::optional<boost::system::error_code> *result)
{
    if(error == boost::asio::error::operation_aborted) return;
    result->reset(error);
    if(error) return;
    bytes = transferred;
}


static void read_completed2(boost::shared_ptr<boost::asio::io_service> io, const boost::system::error_code& error, size_t transferred, size_t &bytes, boost::optional<boost::system::error_code> *result)
{
    io->post(boost::bind(read_completed, error, transferred, std::ref(bytes), result));
}


static void timeout_expired(const boost::system::error_code& error, boost::optional<boost::system::error_code> *result)
{
    if(error == boost::asio::error::operation_aborted) return;
    result->reset(error);
}

size_t TcpClient::read_with_timeout()
{
    size_t bytes = 0;
    m_Response.clear();
    m_IoService->reset();

    boost::optional<boost::system::error_code> timer_result;
    boost::optional<boost::system::error_code> read_result;

    boost::asio::deadline_timer timer(*m_IoService);
    timer.expires_from_now(boost::posix_time::seconds(m_Timeout));
    timer.async_wait(boost::bind(timeout_expired, boost::asio::placeholders::error, &timer_result));

    auto read_callback = boost::bind(read_completed, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, std::ref(bytes), &read_result);

    if(ssl())
        boost::asio::async_read_until(m_Stream, m_Streambuf, m_Delimiter, read_callback);
    else
        boost::asio::async_read_until(m_Stream.next_layer(), m_Streambuf, m_Delimiter, read_callback);

    while(m_IoService->run_one())
    {
        if(read_result) timer.cancel();
        if(timer_result) m_Stream.next_layer().cancel();
    }

    if (timer_result)
        throw std::runtime_error("async read timeout");

    if (read_result && *read_result)
        throw std::runtime_error("read failed: " + read_result->message());

    m_Response.resize(bytes - m_Delimiter.size());
    {
        std::istream is(&m_Streambuf);
        is.read(const_cast<char *>(m_Response.data()), m_Response.size());
    }
    m_Streambuf.consume(m_Delimiter.size());
    return bytes;
}


std::size_t TcpClient::read()
{
    if(m_OwnIO) return read_with_timeout();
    return read(m_Streambuf);
}

std::size_t TcpClient::read(boost::asio::streambuf& buf)
/*
 *  SOME SPECIAL CASE
 *  buf may be not empty
 */
{
    size_t bytes = 0;

    m_Response.clear();
    m_IoService->reset();

    boost::optional<boost::system::error_code> timer_result;
    boost::optional<boost::system::error_code> read_result;

    boost::asio::deadline_timer timer(*m_IoService);
    timer.expires_from_now(boost::posix_time::seconds(m_Timeout));
    timer.async_wait(boost::bind(timeout_expired, boost::asio::placeholders::error, &timer_result));

    if(m_OwnIO)
    {
        auto read_callback = boost::bind(read_completed, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, std::ref(bytes), &read_result);
        if(ssl())
            boost::asio::async_read_until(m_Stream, buf, m_Delimiter, read_callback);
        else
            boost::asio::async_read_until(m_Stream.next_layer(), buf, m_Delimiter, read_callback);
    }
    else
    {
        if(!m_ExternalStrand)
            throw std::runtime_error("strand not initialized");

        auto read_callback = m_ExternalStrand->wrap(boost::bind(read_completed2, m_IoService, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, std::ref(bytes), &read_result));
        if(ssl())
            boost::asio::async_read_until(m_Stream, buf, m_Delimiter, read_callback);
        else
            boost::asio::async_read_until(m_Stream.next_layer(), buf, m_Delimiter, read_callback);
    }

    while(m_IoService->run_one())
    {
        if(read_result)
        {
            timer.cancel();
        }

        if(timer_result)
        {
            // Поскольку мы получили таймаут на чтении, теперь можно отменить чтение
            if(!m_ExternalStrand)
            {
                m_Stream.next_layer().cancel();
            }
            else
            {
                // В случае с инстантом надо делать это под стрендом
                auto canceller = boost::bind(&boost::asio::ip::tcp::socket::cancel, &m_Stream.next_layer());
                m_ExternalStrand->dispatch(canceller);
            }

            // Поскольку мы отменили чтение, в очередь реактора io_service поставился хендлер чтения с ошибкой operation::aborted
            // Следовательно, прежде чем завершать чтение, надо выполнить этот хендлер
            // Нюансы:
            // - В случае !m_OwnIO (работа в instant'e), постановка хендлера в очередь осуществляется в другом потоке другим io_service
            // - В этом же случае мы имеем разнесенные по времени события: постановку хендлера в очередь и вызов run_one();
            // run_one() может быть вызван быстрее, и тогда нужно вызывать io_service->reset(), так как если run_one() вызвать тогда, когда
            // в очереди io_service нет событий, io_sevice "ломается" и всегда мгновенно возвращает 0.

            while(!m_IoService->run_one())
            {
#if defined(BOOST_OS_WINDOWS)
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
#else
                usleep(1000);           // 1 ms
#endif
                m_IoService->reset();
            }
        }
    }

    if (timer_result)
        throw std::runtime_error("async read timeout");

    if (read_result && *read_result)
        throw std::runtime_error("read failed: " + read_result->message());

    m_Response.resize(bytes - m_Delimiter.size());
    {
        std::istream is(&buf);
        is.read(const_cast<char *>(m_Response.data()), m_Response.size());
    }
    buf.consume(m_Delimiter.size());
    return bytes;
}


std::size_t TcpClient::read(std::size_t need_bytes, int timeout)
{
    m_Response.clear();
    m_IoService->reset();
    m_Response.resize(need_bytes);
    char *resp = const_cast<char *>(m_Response.data());

    size_t already_in_stream = std::min(need_bytes, m_Streambuf.size());
    if (already_in_stream)
    {
        std::istream is(&m_Streambuf);
        is.read(resp, already_in_stream);
    }
    if (already_in_stream == need_bytes) return need_bytes;

    boost::optional<boost::system::error_code> timer_result;
    boost::optional<boost::system::error_code> read_result;

    boost::asio::deadline_timer timer(*m_IoService);
    timer.expires_from_now(boost::posix_time::seconds(timeout));
    timer.async_wait(boost::bind(timeout_expired, boost::asio::placeholders::error, &timer_result));

    auto buffer = boost::asio::buffer(resp + already_in_stream, need_bytes - already_in_stream);
    auto condition = boost::asio::transfer_exactly(need_bytes - already_in_stream);
    size_t bytes = 0;

    if(m_OwnIO)
    {
        auto read_callback = boost::bind(read_completed, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, std::ref(bytes), &read_result);
        if(ssl())
            boost::asio::async_read(m_Stream, buffer, condition, read_callback);
        else
            boost::asio::async_read(m_Stream.next_layer(), buffer, condition, read_callback);
    }
    else
    {
        auto read_callback = m_ExternalStrand->wrap(boost::bind(read_completed2, m_IoService, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, std::ref(bytes), &read_result));
        if(ssl())
            boost::asio::async_read(m_Stream, buffer, condition, read_callback);
        else
            boost::asio::async_read(m_Stream.next_layer(), buffer, condition, read_callback);
    }


    while(m_IoService->run_one())
    {
        if(read_result)
        {
            timer.cancel();
        }

        if(timer_result)
        {
            // Поскольку мы получили таймаут на чтении, теперь можно отменить чтение
            if(!m_ExternalStrand)
            {
                m_Stream.next_layer().cancel();
            }
            else
            {
                // В случае с инстантом надо делать это под стрендом
                auto canceller = boost::bind(&boost::asio::ip::tcp::socket::cancel, &m_Stream.next_layer());
                m_ExternalStrand->dispatch(canceller);
            }

            // Поскольку мы отменили чтение, в очередь реактора io_service поставился хендлер чтения с ошибкой operation::aborted
            // Следовательно, прежде чем завершать чтение, надо выполнить этот хендлер
            // Нюансы:
            // - В случае !m_OwnIO (работа в instant'e), постановка хендлера в очередь осуществляется в другом потоке другим io_service
            // - В этом же случае мы имеем разнесенные по времени события: постановку хендлера в очередь и вызов run_one();
            // run_one() может быть вызван быстрее, и тогда нужно вызывать io_service->reset(), так как если run_one() вызвать тогда, когда
            // в очереди io_service нет событий, io_sevice "ломается" и всегда мгновенно возвращает 0.

            while(!m_IoService->run_one())
            {
#if defined(BOOST_OS_WINDOWS)
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
#else
                usleep(1000);           // 1 ms
#endif
                m_IoService->reset();
            }
        }
    }

    if (timer_result)
        throw std::runtime_error("async read timeout");

    if (read_result && *read_result)
        throw std::runtime_error("read failed: " + read_result->message());

    return need_bytes;
}

std::size_t TcpClient::write(std::string const& str)
{
    boost::system::error_code ec;
    std::size_t bytes;
    if(ssl())
        bytes = boost::asio::write(m_Stream, boost::asio::buffer( str.data(), str.size()), ec);
    else
        bytes = boost::asio::write(m_Stream.next_layer(), boost::asio::buffer( str.data(), str.size()), ec);

    if(ec) throw std::runtime_error("write failed: " + ec.message());
    return bytes;
}

std::string TcpClient::local_addr()
{
    boost::system::error_code ec;
    boost::asio::ip::address addr;
    addr = m_Stream.next_layer().local_endpoint().address();
    std::string string_addr = addr.to_string(ec);
    if(ec) throw std::runtime_error("get local address failed");
    return string_addr;
}

bool TcpClient::isOpen()
{
    bool ret = false;

    if(!m_IsOpen)
        return false;

    ret = m_Stream.next_layer().is_open();
    return ret;
}

void TcpClient::do_close()
{
    // throws exception "close: Bad file descriptor"
    m_Stream.next_layer().close();
}


void TcpClient::close()
{
    if(!m_ExternalStrand)
    {
        do_close();
    }
    else
    {
        //auto closer = boost::bind( &boost::asio::ip::tcp::socket::close, &m_Stream.next_layer());
        auto closer = boost::bind(&TcpClient::do_close, shared_from_this());
        m_ExternalStrand->dispatch(closer);
    }
    m_IsOpen = false;
}

void TcpClient::shutdown()
{
    // DON'T USE IT !!!
    if(ssl())
        m_Stream.shutdown();
    else
        m_Stream.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
}

/*********************************************************************
                HTTP CLIENT
*********************************************************************/

HttpClient::HttpClient(unsigned timeout):
    TcpClient(timeout)
{
    m_Delimiter="\r\n\r\n";
}

std::size_t HttpClient::read()
{
    // Читаем заголовки
    std::size_t header_size = TcpClient::read();

    std::swap(m_HttpHeaders, m_Response);
    m_Response.clear();

    size_t pos;

    std::string location = "Location: ";
    pos = m_HttpHeaders.find(location);
    if (pos != std::string::npos)
    {
        size_t pos2 = m_HttpHeaders.find("\r\n", pos + location.size());
        m_Location = m_HttpHeaders.substr(pos + location.size(), pos2 - pos - location.size());
    }

    size_t body_size = 0;
    std::string cont_len = "Content-Length: ";
    pos = m_HttpHeaders.find(cont_len);
    if (pos != std::string::npos)
        body_size = atoi(m_HttpHeaders.c_str() + pos + cont_len.size());

    // Читаем тело http-ответа
    if (body_size)
    {
        TcpClient::read(body_size, m_Timeout);
    }
    else
    {
        std::string transfer_encoding = "Transfer-Encoding: ";
        pos = m_HttpHeaders.find(transfer_encoding);
        if (pos != std::string::npos)
        {
            std::string chunked = "chunked";
            std::string transfer_encoding_value = m_HttpHeaders.substr(pos + transfer_encoding.size(), chunked.size());
            if(transfer_encoding_value != chunked)
                throw std::runtime_error("No content length available, transfer_encoding: " + transfer_encoding_value);
        }
        else
            throw std::runtime_error("No content length available");

        // is chunked!
        std::string tmp;
        m_Delimiter = "\r\n";
        // FIXME: http://ru.wikipedia.org/wiki/Chunked_transfer_encoding
        // Наверное, стоит придумать алгоритм получше :)
        while(1)
        {
            TcpClient::read();
            int chunk_size = strtol(m_Response.c_str(), NULL, 16);
            body_size += chunk_size;

            std::cerr << "CUNK SIZE: " << chunk_size << "\n";

            if(chunk_size == 0)
                break;

            TcpClient::read(chunk_size + 2/*+CRLF*/, m_Timeout);
            tmp += m_Response;
        }
        m_Response = tmp;
        m_Delimiter = "\r\n\r\n";
    }

    return (header_size + body_size);
}

int HttpClient::responseCode() const throw (std::exception)
/*
 *  http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html
 */
{
    if(m_HttpHeaders.empty())
        throw std::runtime_error("http header empty");

    size_t pos = m_HttpHeaders.find("\r\n");
    if(pos == std::string::npos)
        throw std::runtime_error("invalid http header");

    std::string status = m_HttpHeaders.substr(0, pos);

    // status like:
    // HTTP/1.1 200 OK
    // HTTP/1.1 301 Moved Permanently

    std::vector<std::string> vec;
    UTIL::split(status, " ", vec);

    if(vec.size() < 2)
        throw std::runtime_error("invalid http header: " + status);

    return UTIL::s2i(vec[1]);
}


