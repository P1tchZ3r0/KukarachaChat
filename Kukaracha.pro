TEMPLATE = subdirs

CONFIG += ordered

SUBDIRS += \
    common \
    server \
    client

client.depends = common
server.depends = common

