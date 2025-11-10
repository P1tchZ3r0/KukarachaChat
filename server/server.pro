TEMPLATE = app
CONFIG += console c++20

QT += core network

TARGET = KukarachaServer

INCLUDEPATH += $$PWD/src \
                $$PWD/../common/src

HEADERS += \
    src/ChatServer.h \
    src/ClientConnection.h \
    src/UserStore.h

SOURCES += \
    src/main.cpp \
    src/ChatServer.cpp \
    src/ClientConnection.cpp \
    src/UserStore.cpp

LIBS += -L$$OUT_PWD/../common -lKukarachaCommon

DEPENDPATH += $$PWD/../common/src

