#ifndef CENTRAL_WIDGET_H
#define CENTRAL_WIDGET_H

#include <QWidget>
#include <QLineEdit>

#include "tabs.h"

class CentralWidget : public QWidget
{
    Q_OBJECT

public slots:
    void getURL();

public:
    CentralWidget(QWidget* parent = NULL);
    virtual ~CentralWidget();

    // GETTERS
    std::shared_ptr<Tabs> tabs() const {return m_Tabs;}
    std::shared_ptr<QLineEdit> searchLine() const {return m_LineEdit;}

private:

    std::shared_ptr<Tabs> m_Tabs;
    std::shared_ptr<QLineEdit> m_LineEdit;


};


#endif // CENTRAL_WIDGET_H
