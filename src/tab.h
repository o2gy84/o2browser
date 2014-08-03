#ifndef TAB_H
#define TAB_H

#include <QWidget>
#include <QTextLayout>
#include <QPainter>
#include <QTextEdit>
#include <QVBoxLayout>

#include <memory>

#include "client.h"
#include "page.h"


class Tab: public QWidget
{
    Q_OBJECT
public slots:
    //void paintEvent(QPaintEvent *event);
    void reloadPage();

signals:
    void needReloadPage();


public:
     Tab(QWidget* parent = NULL);
     virtual ~Tab();

     // WORK
     void doUrl(const std::string &url);

private:
     int realDoUrl(const std::string &url, std::string &location);

private:
     std::string m_Headers;
     std::string m_Body;
     std::vector<std::string> m_Links;

     std::shared_ptr<HttpClient> m_Client;

     std::shared_ptr<Page> m_Page;
     std::shared_ptr<QVBoxLayout> m_VertLayout;
};

#endif // TAB_H
