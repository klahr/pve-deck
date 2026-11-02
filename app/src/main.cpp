// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QtQml>

#include <sailfishapp.h>

#include "appinfo.h"
#include "clusteroverview.h"
#include "guestdetails.h"
#include "linechart.h"
#include "poweractions.h"
#include "pveconsolesession.h"
#include "pveclient.h"
#include "resourcefiltermodel.h"
#include "resourcemodel.h"
#include "rrdhistory.h"
#include "sailfishsecretbackend.h"
#include "serverstore.h"
#include "sessionmanager.h"
#include "terminalitem.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match [X-Sailjail] in pvedeck.desktop.
    app->setOrganizationName(QStringLiteral("io.github.klahr"));
    app->setApplicationName(QStringLiteral("pvedeck"));
    app->setApplicationVersion(QStringLiteral(APP_VERSION));

    qmlRegisterUncreatableType<PveClient>("PveDeck", 1, 0, "PveClient",
                                          QStringLiteral("Get clients from sessionManager.client()"));
    qmlRegisterUncreatableType<ResourceModel>("PveDeck", 1, 0, "ResourceModel",
                                              QStringLiteral("Get resources from sessionManager.resources()"));
    qmlRegisterType<ResourceFilterModel>("PveDeck", 1, 0, "ResourceFilterModel");
    qmlRegisterType<GuestDetails>("PveDeck", 1, 0, "GuestDetails");
    qmlRegisterType<ClusterOverview>("PveDeck", 1, 0, "ClusterOverview");
    qmlRegisterType<RrdHistory>("PveDeck", 1, 0, "RrdHistory");
    qmlRegisterType<LineChart>("PveDeck", 1, 0, "LineChart");
    qmlRegisterUncreatableType<TerminalBackend>("PveDeck", 1, 0, "TerminalBackend",
                                                QStringLiteral("Use PveConsoleSession"));
    qmlRegisterType<PveConsoleSession>("PveDeck", 1, 0, "PveConsoleSession");
    qmlRegisterType<TerminalItem>("PveDeck", 1, 0, "TerminalItem");

    AppInfo appInfo(QStringLiteral(APP_VERSION));
    // Sailjail only allows writing inside ~/.config/<org>/<app>/, not the
    // default QSettings file next to it.
    ServerStore serverStore(new QSettings(
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/pvedeck.conf"), QSettings::NativeFormat));
    SailfishSecretBackend secrets;
    SessionManager sessionManager(&serverStore, &secrets);
    PowerActions powerActions(&sessionManager);

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    QQmlContext *context = view->rootContext();
    context->setContextProperty(QStringLiteral("appInfo"), &appInfo);
    context->setContextProperty(QStringLiteral("serverStore"), &serverStore);
    context->setContextProperty(QStringLiteral("sessionManager"), &sessionManager);
    context->setContextProperty(QStringLiteral("powerActions"), &powerActions);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    const int result = app->exec();
    // Delete the view first: QML may still reference the objects above.
    view.reset();
    return result;
}
