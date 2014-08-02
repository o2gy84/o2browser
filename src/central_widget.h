#ifndef CENTRAL_WIDGET_H
#define CENTRAL_WIDGET_H

#include "tabs.h"

#include <QWidget>
#include <QLineEdit>


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
    std::shared_ptr<QLineEdit> line() const {return m_LineEdit;}

private:

    std::shared_ptr<Tabs> m_Tabs;
    std::shared_ptr<QLineEdit> m_LineEdit;


};


#endif // CENTRAL_WIDGET_H
