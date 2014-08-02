#ifndef CLIENT_H
#define CLIENT_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>

class TcpClient: public boost::enable_shared_from_this<TcpClient>
{
public:
    TcpClient(unsigned timeout);
    TcpClient(boost::asio::io_service& io, boost::asio::io_service::strand *strand, unsigned timeout);

    void connect(const std::string& host, const std::string& port) throw (std::exception);
    void handshake() throw (std::exception);
    std::size_t read(boost::asio::streambuf& buf);
    std::size_t read(std::size_t need_bytes, int timeout);

    std::size_t write(std::string const& cmd);
    std::string local_addr();
    bool isOpen();
    void close();
    void shutdown();

    const std::string& response() const { return m_Response; }
    void response(std::string &res) { std::swap(m_Response, res); }

    bool ssl() {return m_IsSSL;}
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream() {return m_Stream;}

    void setOptionSSL(bool ssl) {m_IsSSL = ssl;}
    void setConnected(bool val) {m_IsOpen = val;}

    virtual std::size_t read();


private:
    boost::asio::ip::tcp::resolver::iterator resolve(const std::string& host, const std::string& port);
    size_t read_with_timeout();
    void do_close();

protected:
    std::string m_Response;
    std::string m_Delimiter;
    unsigned m_Timeout;

private:

    boost::asio::io_service::strand *m_ExternalStrand;    // need only from instant

    boost::asio::streambuf m_Streambuf;

    boost::shared_ptr<boost::asio::io_service> m_IoService;

    boost::asio::ssl::context m_Ctx;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_Stream;

    bool m_IsSSL;
    bool m_OwnIO;

    bool m_IsOpen;
};


class HttpClient: public TcpClient
{
public:
    HttpClient(unsigned timeout);

    virtual std::size_t read();

public:
    std::string const& getHttpHeaders() const { return m_HttpHeaders; }
    int responseCode() const throw (std::exception);

private:
    std::string m_HttpHeaders;
};



#endif // CLIENT_H
