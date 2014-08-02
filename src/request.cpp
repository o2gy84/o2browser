#include <exception>
#include <stdexcept>

#include "request.h"
#include "util.h"

Request::Request(const std::string& url)
{
    if(!UTIL::isBeginFromUrl(url.c_str(), url.size()))
        throw std::runtime_error("invalid url: " + url);

    if((url.size() > 2) && url[0] == '/' && url[1] == '/')
        m_Host = url.substr(2, std::string::npos);
    else if(UTIL::startsWith(url, "https://") && url.size() > sizeof("https://"))
        m_Host = url.substr(sizeof("https://") - 1, std::string::npos);
    else if(UTIL::startsWith(url, "http://") && url.size() > sizeof("http://"))
        m_Host = url.substr(sizeof("http://") - 1, std::string::npos);
    else
        throw std::runtime_error("invalid url: " + url);

    std::string params = "/";

    std::size_t pos = m_Host.find('/');
    if(pos != std::string::npos)
    {
        params = m_Host.substr(pos, std::string::npos);
        m_Host = m_Host.substr(0, pos);
    }

    m_GetRequestString = "GET " + params;
    m_GetRequestString += std::string(" HTTP/1.1\r\nHost: ") + m_Host;
    {
        //m_GetRequestString += std::string("\r\nUser-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/36.0.1985.125 Safari/537.36");
        m_GetRequestString += std::string("\r\nUser-Agent: o2browser");
        //m_GetRequestString += std::string("\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    }
    m_GetRequestString += "\r\n\r\n";

    std::string post_params = "";
    pos = params.find('?');
    if(pos != std::string::npos)
    {
        post_params = params.substr(pos + 1, std::string::npos);
        params = params.substr(0, params.size() - 1);
    }

    m_PostRequestString = "POST ";
    m_PostRequestString += params;
    m_PostRequestString += std::string(" HTTP/1.1\r\nHost: ") + m_Host;
    m_PostRequestString += "\r\n\r\n";
    m_PostRequestString += post_params;
}

