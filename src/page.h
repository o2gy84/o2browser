#ifndef PAGE_H
#define PAGE_H

#include <memory>

#include <QWidget>
#include <QTextEdit>
#include <QTextLayout>
#include <QPainter>



class Page: public QWidget
{
    Q_OBJECT
public:
     Page(QWidget* parent = NULL);
     virtual ~Page();

     std::shared_ptr<QTextEdit> headersArea() const {return m_ResponseHeadersArea;}
     std::shared_ptr<QTextEdit> bodyArea() const {return m_ResponseBodyArea;}
     std::shared_ptr<QTextEdit> linksArea() const {return m_ResponseLinksArea;}

     void showContent(const std::string &headers, const std::string &body,
                      const std::vector<std::string> &links);

     // not used
     void showContentOverTextLayout();

protected:
     void makeLayout();

private:

     std::shared_ptr<QTextLayout> m_Layout;
     std::shared_ptr<QPainter> m_Painter;

     std::shared_ptr<QTextEdit> m_ResponseHeadersArea;
     std::shared_ptr<QTextEdit> m_ResponseBodyArea;
     std::shared_ptr<QTextEdit> m_ResponseLinksArea;

     bool m_DrawOverTextLayout;
};

#endif // PAGE_H
