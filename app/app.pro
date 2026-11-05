TARGET = pvedeck

CONFIG += link_pkgconfig sailfishapp sailfishapp_i18n sailfishapp_i18n_idbased sailfishapp_i18n_unfinished c++11
QT += network qml quick
PKGCONFIG += sailfishsecrets

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

include(../core/core.pri)

HEADERS += \
    src/sailfishsecretbackend.h \
    src/terminalitem.h \
    src/linechart.h

SOURCES += \
    src/main.cpp \
    src/sailfishsecretbackend.cpp \
    src/terminalitem.cpp \
    src/linechart.cpp

DISTFILES += \
    qml/pvedeck.qml \
    qml/cover/CoverPage.qml \
    qml/components/LoginFlow.qml \
    qml/components/GuestStatus.qml \
    qml/components/UsageBar.qml \
    qml/components/TerminalKey.qml \
    qml/components/HistoryChart.qml \
    qml/pages/ServerListPage.qml \
    qml/pages/ServerDialog.qml \
    qml/pages/LoginDialog.qml \
    qml/pages/TfaDialog.qml \
    qml/pages/CertificateDialog.qml \
    qml/pages/SessionPage.qml \
    qml/pages/GuestPage.qml \
    qml/pages/ClusterPage.qml \
    qml/pages/ConsolePage.qml \
    qml/pages/AboutPage.qml \
    pvedeck.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

TRANSLATIONS += translations/pvedeck.ts
