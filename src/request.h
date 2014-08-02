#ifndef REQUEST_H
#define REQUEST_H

#include <string>

class Request
{
public:
    Request(const std::string& url);
public:

    const std::string& host() const {return m_Host;}
    std::string host() {return m_Host;}

    std::string toGetRequsetString() {return m_GetRequestString;}
    std::string toPostRequsetString() {return m_PostRequestString;}

private:
    std::string m_GetRequestString;
    std::string m_PostRequestString;
    std::string m_Host;
};


#endif // REQUEST_H
