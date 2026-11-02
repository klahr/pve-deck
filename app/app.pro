TARGET = pvedeck

CONFIG += sailfishapp sailfishapp_i18n sailfishapp_i18n_idbased c++11 link_pkgconfig
QT += network qml
PKGCONFIG += sailfishsecrets

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

include(../core/core.pri)

HEADERS += \
    src/sailfishsecretbackend.h

SOURCES += \
    src/main.cpp \
    src/sailfishsecretbackend.cpp

DISTFILES += \
    qml/pvedeck.qml \
    qml/cover/CoverPage.qml \
    qml/components/LoginFlow.qml \
    qml/pages/ServerListPage.qml \
    qml/pages/ServerDialog.qml \
    qml/pages/LoginDialog.qml \
    qml/pages/TfaDialog.qml \
    qml/pages/CertificateDialog.qml \
    qml/pages/SessionPage.qml \
    qml/pages/AboutPage.qml \
    pvedeck.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

TRANSLATIONS += translations/pvedeck-sv.ts
