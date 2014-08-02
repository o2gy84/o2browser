#include <iostream>
#include <memory>

#include <QFont>
#include <QFontMetrics>

#include "tab.h"
#include "util.h"
#include "request.h"
#include "html_parser.h"

Tab::Tab(QWidget* parent)
    :
      m_Layout(new QTextLayout())
{
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
        if(http_code == 301 && !location.empty())
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

        std::cerr<<"REQUEST:\n" << request << "\n";

        m_Client->write(request);
        m_Client->read();

        location = m_Client->getLocation();
        ret = m_Client->responseCode();

        std::cerr << "RESPONSE:\n";
        std::cerr << m_Client->getHttpHeaders() << "\n";
        //std::cerr << "***\n";
        //std::cerr << m_Client->response() << "\n";

        HtmlParser parser;
        parser.parse(m_Client->response());
        std::cerr << "PARSER HTML: " << parser.getHtml().size() << " bytes\n";
        std::cerr << parser.getHtml() << "\n";

        std::cerr << "PARSER PLAIN: " << parser.getPlain().size() << " bytes\n";
        std::cerr << parser.getPlain() << "\n";

        m_Content = m_Client->getHttpHeaders();
        m_Content += "****************\n";
        m_Content += "****************\n";
        //m_Content += m_Client->response();
        m_Content += parser.getPlain();
    }
    catch(std::exception &e)
    {
        m_Content = e.what();
        m_Content += " [host: " + host + ", port: " + UTIL::i2s(port) + "]";
    }

    // generate paint event
    this->update();
    return ret;
}

void Tab::makeLayout()
{
    int size = 10;
    int weigth = -1;

    QFont font("Times", size, weigth);
    QFontMetrics metrics(font);

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    m_Layout->setText(m_Content.c_str());
    m_Layout->setFont(font);
    m_Layout->setTextOption(opt);

    int leading = metrics.leading();

    //m_Layout->setCacheEnabled(true);
    int height = 10;
    m_Layout->beginLayout();
    while (1)
    {
        QTextLine line = m_Layout->createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(this->width());
        height += leading;
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    m_Layout->endLayout();
}

void Tab::paintEvent(QPaintEvent *event)
{
    if(!m_Content.empty())
    {
        makeLayout();
    }
    else
        return;

    if(!m_Painter)
    {
        m_Painter.reset(new QPainter(this));
    }

    m_Painter->begin(this);

    QPen pen;
    //pen.setColor(QColor(0, 0, 127));
    pen.setColor(Qt::black);

    m_Painter->eraseRect(QRect(0, 0, width(), height()));
    m_Painter->setPen(pen);
    m_Layout->draw(m_Painter.get(), QPoint(0, 0));
    m_Painter->end();
}
