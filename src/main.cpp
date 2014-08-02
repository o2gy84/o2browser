#include "mainwindow.h"
#include "central_widget.h"

#include <QApplication>
#include <QVBoxLayout>


//#include <boost/shared_ptr.hpp>
//#include <boost/thread.hpp>


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow *w = new MainWindow();
    w->setWindowTitle(QString::fromUtf8("o2browser"));
    w->resize(640, 800);

    std::shared_ptr<CentralWidget> cw(new CentralWidget(w));
    std::shared_ptr<QVBoxLayout> layout (new QVBoxLayout());
    layout->addWidget(cw->line().get());
    layout->addWidget(cw->tabs().get());
    cw->setLayout(layout.get());

    cw->line().get()->setFocus();
    w->setCentralWidget(cw.get());
    w->show();

    return app.exec();
}
