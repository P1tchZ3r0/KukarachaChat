TEMPLATE = lib
CONFIG += staticlib c++20

QT += core

TARGET = KukarachaCommon

INCLUDEPATH += $$PWD/src

HEADERS += \
    src/ChatMessage.h \
    src/IMessageSerializer.h \
    src/JsonMessageSerializer.h

SOURCES += \
    src/ChatMessage.cpp \
    src/JsonMessageSerializer.cpp

