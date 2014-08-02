#ifndef TAB_H
#define TAB_H

#include <QWidget>
#include <QTextLayout>
#include <QPainter>

#include <memory>

#include "client.h"

class Tab: public QWidget
{
    Q_OBJECT
public slots:
    void paintEvent(QPaintEvent *event);

public:
     Tab(QWidget* parent = NULL);
     virtual ~Tab();

     // WORK
     void doUrl(const std::string &url);

protected:
     void makeLayout();

private:
     int realDoUrl(const std::string &url, std::string &location);

private:
     std::string m_Content;
     std::shared_ptr<QTextLayout> m_Layout;
     std::shared_ptr<QPainter> m_Painter;
     std::shared_ptr<HttpClient> m_Client;
};

#endif // TAB_H
