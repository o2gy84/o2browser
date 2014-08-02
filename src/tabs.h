#ifndef TABS_H
#define TABS_H

#include <vector>
#include <string>
#include <memory>

#include <QTabWidget>
#include <QTabBar>

#include "tab.h"

class Tabs: public QTabWidget
{
    Q_OBJECT

public slots:
    void myItemChangeSlot(int index);
    void closeTab(int index);
    void clicked(int index);
    void doubleClicked(int index);
    void moved(int from, int to);

public:
    Tabs(QWidget* parent = NULL);

    QTabBar* tabBar();                  ///< Overridden method from QTabWidget
    void addTab(const QString &str);

    // WORK
    void doUrl(const std::string &str);

private:

    std::vector<std::shared_ptr<Tab> > m_Tabs;
    int m_PrevDoubleClicked;
};

#endif // TABS_H
