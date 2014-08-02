#include <iostream>

#include "tabs.h"
#include "tab.h"

Tabs::Tabs(QWidget* parent)
    :
      m_PrevDoubleClicked(-1)
{
    QWidget::setParent(parent);
    QObject::connect(this,SIGNAL(currentChanged(int)),this,SLOT(myItemChangeSlot(int)));
    QObject::connect(this,SIGNAL(tabCloseRequested(int)),this,SLOT(closeTab(int)));
    QObject::connect(this, SIGNAL(tabBarClicked(int)), this, SLOT(clicked(int)) );
    QObject::connect(this, SIGNAL(tabBarDoubleClicked(int)), this, SLOT(doubleClicked(int)) );
    QObject::connect(tabBar(), SIGNAL(tabMoved(int, int)), this, SLOT(moved(int, int)) );
}


QTabBar* Tabs::tabBar()
/*
 * Overridden (protected) method from QTabWidget, to make it public
 * return current tab
 */
{
    return QTabWidget::tabBar();
}

void Tabs::addTab(const QString &str)
{
    std::shared_ptr<Tab> t(new Tab());
    m_Tabs.push_back(t);
    this->QTabWidget::addTab( t.get() , str);
}


/***************************
 * SLOTS
 ****************************/
//slot
void Tabs::myItemChangeSlot(int index)
{
    this->tabBar()->setTabTextColor(index, Qt::red);
    for(int i = 0; i<this->tabBar()->count(); ++i)
    {
        if(i != index)
            this->tabBar()->setTabTextColor(i,Qt::black);
    }
}

//slot
void Tabs::closeTab(int index)
//Handle tabCloseRequested Signal and Close the Tab
{
    this->removeTab(index);

    // NOTE: fix this alhoritm in case, when std::vector changed on std::list/std::queue e.t.c
    // must verify the existence of it !
    std::vector<std::shared_ptr<Tab>>::iterator it = m_Tabs.begin() + index;
    m_Tabs.erase(it);
}

//slot
void Tabs::clicked(int index)
{
    //std::cerr << "Clicked: " << index << "\n";
}

//slot
void Tabs::doubleClicked(int index)
{
    /*
    if(m_PrevDoubleClicked == -1)
    {
        m_PrevDoubleClicked = index;
        return;
    }

    if(m_PrevDoubleClicked != index)
    {
        m_PrevDoubleClicked = index;
        return;
    }
    */

    m_PrevDoubleClicked = index;

    std::shared_ptr<Tab> t(new Tab());
    std::vector<std::shared_ptr<Tab>>::iterator it = m_Tabs.begin() + index + 1;
    m_Tabs.insert(it, t);
    insertTab(index + 1, t.get(), "default");
}

//slot
void Tabs::moved(int from, int to)
{
    std::swap(m_Tabs[from], m_Tabs[to]);
}


void Tabs::doUrl(const std::string &str)
{
    int index = tabBar()->currentIndex();
    this->tabBar()->setTabText(index, str.c_str());
    m_Tabs[index]->doUrl(str);
}
