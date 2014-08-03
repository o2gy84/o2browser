#include <iostream>
#include <memory>

#include <QFont>
#include <QFontMetrics>
#include <QSizePolicy>

#include "tab.h"
#include "util.h"
#include "request.h"
#include "html_parser.h"

Tab::Tab(QWidget* parent):
    QWidget(parent),
    m_Page(new Page(this))
{
    QSizePolicy sp;
    sp.setHorizontalPolicy(QSizePolicy::Maximum);

    m_Page->headersArea().get()->setFixedHeight(70);
    m_Page->linksArea().get()->setFixedHeight(100);
    //m_Page->headersArea().get()->setSizePolicy(sp);
    //m_Page->headersArea().get()->setSizeAdjustPolicy();

    m_Page->headersArea().get()->setReadOnly(true);
    m_Page->linksArea().get()->setReadOnly(true);
    m_Page->bodyArea().get()->setReadOnly(true);

    m_VertLayout.reset(new QVBoxLayout(this));
    m_VertLayout->addWidget(m_Page->headersArea().get());
    m_VertLayout->addWidget(m_Page->linksArea().get());
    m_VertLayout->addWidget(m_Page->bodyArea().get());

    setLayout(m_VertLayout.get());

    QObject::connect(this, SIGNAL(needReloadPage()), this, SLOT(reloadPage()));
}

Tab::~Tab()
{
}

void Tab::doUrl(const std::string &url)
{
    int count = 5;
    int http_code = -1;
    std::string location;
    std::string real_url = url;

    for(int i = 0; i < count; ++i)
    {
        http_code = realDoUrl(real_url, location);
        if( (http_code > 300) && (http_code < 310) && !location.empty())
        {
            std::cerr << "REDIRECT!!!\n";
            real_url = location;
            continue;
        }

        break;
    }
}

int Tab::realDoUrl(const std::string &url, std::string &location)
{
    int ret = -1;
    bool ssl = UTIL::startsWith(url, "https");
    int port = (ssl)? 443 : 80;
    unsigned read_timeout = 5;  // sec
    std::string host;

    try
    {
        Request req(url);
        host = req.host();

        m_Client.reset(new HttpClient(read_timeout));
        m_Client->setOptionSSL(ssl);
        m_Client->connect(host, UTIL::i2s(port));
        m_Client->handshake();

        std::string request = req.toGetRequsetString();

        std::cerr << "\nREQUEST:\n" << request << "\n";

        m_Client->write(request);
        int bytes = m_Client->read();
        std::cerr << "read bytes: " << bytes << "\n";

        location = m_Client->getLocation();
        ret = m_Client->responseCode();

        std::cerr << "\nRESPONSE:\n";
        std::cerr << m_Client->getHttpHeaders() << "\n";
        //std::cerr << "***\n";
        //std::cerr << m_Client->response() << "\n";

        HtmlParser parser;
        parser.parse(m_Client->response());
        std::cerr << "\nPARSER HTML: " << parser.getHtml().size() << " bytes\n";
        std::cerr << parser.getHtml() << "\n";

        std::cerr << "\nPARSER PLAIN: " << parser.getPlain().size() << " bytes\n";
        std::cerr << parser.getPlain() << "\n";

        m_Headers = m_Client->getHttpHeaders();
        m_Body = parser.getPlain();
        //m_Body = parser.getHtml();
        m_Links = parser.getLinks();
    }
    catch(std::exception &e)
    {
        m_Body = e.what();
        m_Body += " [host: " + host + ", port: " + UTIL::i2s(port) + "]";
    }

    // generate paint event
    //this->update();

    emit needReloadPage();
    return ret;
}



void Tab::reloadPage()
{
    m_Page->showContent(m_Headers, m_Body, m_Links);
}
