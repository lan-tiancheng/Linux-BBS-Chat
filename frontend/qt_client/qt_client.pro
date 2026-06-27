QT += widgets network
CONFIG += c++11
TEMPLATE = app
TARGET = bbs_chat_qt

SOURCES += \
    main.cpp \
    networkclient.cpp \
    loginwindow.cpp \
    mainwindow.cpp

HEADERS += \
    networkclient.h \
    loginwindow.h \
    mainwindow.h

RESOURCES += qt_client.qrc
