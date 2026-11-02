TEMPLATE = app
TARGET = tst_core
# no_testcase_installs: keep test binaries out of the RPM
CONFIG += testcase no_testcase_installs c++11
CONFIG -= app_bundle
QT = core network qml testlib

include(../../core/core.pri)
INCLUDEPATH += ../common

HEADERS += ../common/mockpveserver.h ../common/mockwebsocket.h
SOURCES += tst_core.cpp
