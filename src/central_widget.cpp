#include "central_widget.h"
#include "util.h"

#include <iostream>

#include <QString>

CentralWidget::CentralWidget(QWidget* parent)
    : m_Tabs(new Tabs(this)),
      m_LineEdit (new QLineEdit(this))
{
    QWidget::setParent(parent);

    m_Tabs->setMinimumSize(320, 400);
    m_Tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    //m_Tabs->setWindowFlags(Qt::FramelessWindowHint);

    m_Tabs->setMovable(true);
    m_Tabs->setTabsClosable(true);
    //m_Tabs->tabBar()->setShape(QTabBar::RoundedWest);

    m_Tabs->addTab("1");
    m_Tabs->addTab("2");
    m_Tabs->addTab("3");

    QObject::connect(m_LineEdit.get(), SIGNAL(returnPressed()), this, SLOT(getURL()) );
}

CentralWidget::~CentralWidget()
{

}


void CentralWidget::getURL()
{
    std::string txt  = m_LineEdit->text().toStdString();
    m_Tabs->doUrl(txt);
}
