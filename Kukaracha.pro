TEMPLATE = subdirs
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060400
CONFIG += ordered

SUBDIRS += \
    common \
    server \
    client

client.depends = common
server.depends = common

