TEMPLATE = app
CONFIG += c++20

QT += core gui widgets network

TARGET = KukarachaClient

INCLUDEPATH += $$PWD/src \
                $$PWD/../common/src

HEADERS += \
    src/ChatClient.h \
    src/MainWindow.h

SOURCES += \
    src/main.cpp \
    src/ChatClient.cpp \
    src/MainWindow.cpp

LIBS += -L$$OUT_PWD/../common -lKukarachaCommon

DEPENDPATH += $$PWD/../common/src

