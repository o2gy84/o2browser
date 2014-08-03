#include <iostream>

#include "page.h"


Page::Page(QWidget* parent):
    m_Layout(new QTextLayout()),
    m_ResponseHeadersArea(new QTextEdit(this)),
    m_ResponseBodyArea(new QTextEdit(this)),
    m_ResponseLinksArea(new QTextEdit(this))
{
    QWidget::setParent(parent);
    m_DrawOverTextLayout = false;
}

Page::~Page()
{
}

void Page::makeLayout()
{
    int size = 10;
    int weigth = -1;

    QFont font("Times", size, weigth);
    QFontMetrics metrics(font);

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    m_Layout->setText("here must be a real body text!!");
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

void Page::showContentOverTextLayout()
{
    // This function actually called!
    // But not used in case m_DrawOverTextLayout = false...
    // But may be would be used :)
/*
    if(m_DrawOverTextLayout)
    {
        if(!m_Headers.empty())
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
    else
    {
    }
*/
}

void Page::showContent(const std::string &headers, const std::string &body,
                                 const std::vector<std::string> &links)
{
    QString qstr_h = QString::fromUtf8(headers.data());
    m_ResponseHeadersArea->setPlainText(qstr_h);

    QString qstr_b = QString::fromUtf8(body.data());
    m_ResponseBodyArea->setPlainText(qstr_b);
    //m_ResponseBodyArea->setHtml(qstr_b);

    QString qstr_l;
    for(int i = 0; i < links.size(); ++i)
    {
        if(i) qstr_l += "\n";
        qstr_l += QString::fromUtf8(links[i].data());
    }
    m_ResponseLinksArea->setPlainText(qstr_l);

    m_ResponseBodyArea->show();
    m_ResponseHeadersArea->show();
    m_ResponseLinksArea->show();
}
