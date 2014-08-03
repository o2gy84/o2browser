#-------------------------------------------------
#
# Project created by QtCreator 2014-06-14T00:07:20
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = o2browser
TEMPLATE = app

QMAKE_CXXFLAGS += -std=c++11

INCLUDEPATH += C:\local\include

LIBS += -LC:\local\lib\boost155 -LC:\local\lib\openssl
LIBS += -lboost_system-mgw48-d-1_55 -lboost_thread-mgw48-mt-1_55 -lssl -lcrypto -lws2_32

SOURCES += main.cpp\
    mainwindow.cpp \
    tabs.cpp \
    tab.cpp \
    central_widget.cpp \
    util.cpp \
    client.cpp \
    request.cpp \
    response.cpp \
    html_parser.cpp \
    page.cpp

HEADERS  += mainwindow.h \
    tabs.h \
    tab.h \
    central_widget.h \
    util.h \
    client.h \
    request.h \
    response.h \
    html_parser.h \
    page.h
