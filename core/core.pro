# Platform-independent logic (API client, models, protocol code).
# Static library so it can be unit tested without Silica.
TEMPLATE = lib
CONFIG += staticlib c++11
TARGET = pvedeckcore
QT = core network qml
INCLUDEPATH += ../3rdparty/libvterm/include

HEADERS += \
    appinfo.h \
    formencoding.h \
    serverprofile.h \
    serverstore.h \
    secretbackend.h \
    memorysecretbackend.h \
    pveclient.h \
    sessionmanager.h \
    resourcemodel.h \
    resourcefiltermodel.h \
    guestdetails.h \
    taskwatcher.h \
    poweractions.h \
    websocketclient.h \
    consoleprotocol.h \
    terminalbackend.h \
    pveconsolesession.h \
    terminalscreen.h \
    clusteroverview.h \
    rrdhistory.h \
    tfachallenge.h

SOURCES += \
    appinfo.cpp \
    formencoding.cpp \
    serverprofile.cpp \
    serverstore.cpp \
    memorysecretbackend.cpp \
    pveclient.cpp \
    sessionmanager.cpp \
    resourcemodel.cpp \
    resourcefiltermodel.cpp \
    guestdetails.cpp \
    taskwatcher.cpp \
    poweractions.cpp \
    websocketclient.cpp \
    consoleprotocol.cpp \
    pveconsolesession.cpp \
    terminalscreen.cpp \
    clusteroverview.cpp \
    rrdhistory.cpp \
    tfachallenge.cpp
