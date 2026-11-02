# Platform-independent logic (API client, models, protocol code).
# Static library so it can be unit tested without Silica.
TEMPLATE = lib
CONFIG += staticlib c++11
TARGET = pvedeckcore
QT = core network qml

HEADERS += \
    appinfo.h \
    formencoding.h \
    serverprofile.h \
    serverstore.h \
    secretbackend.h \
    memorysecretbackend.h \
    pveclient.h \
    sessionmanager.h \
    tfachallenge.h

SOURCES += \
    appinfo.cpp \
    formencoding.cpp \
    serverprofile.cpp \
    serverstore.cpp \
    memorysecretbackend.cpp \
    pveclient.cpp \
    sessionmanager.cpp \
    tfachallenge.cpp
