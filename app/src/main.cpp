// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <QtQml>

#include <sailfishapp.h>

#include "appinfo.h"
#include "pveclient.h"
#include "sailfishsecretbackend.h"
#include "serverstore.h"
#include "sessionmanager.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match [X-Sailjail] in pvedeck.desktop.
    app->setOrganizationName(QStringLiteral("io.github.klahr"));
    app->setApplicationName(QStringLiteral("pvedeck"));
    app->setApplicationVersion(QStringLiteral(APP_VERSION));

    qmlRegisterUncreatableType<PveClient>("PveDeck", 1, 0, "PveClient",
                                          QStringLiteral("Get clients from sessionManager.client()"));

    AppInfo appInfo(QStringLiteral(APP_VERSION));
    ServerStore serverStore;
    SailfishSecretBackend secrets;
    SessionManager sessionManager(&serverStore, &secrets);

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    QQmlContext *context = view->rootContext();
    context->setContextProperty(QStringLiteral("appInfo"), &appInfo);
    context->setContextProperty(QStringLiteral("serverStore"), &serverStore);
    context->setContextProperty(QStringLiteral("sessionManager"), &sessionManager);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    const int result = app->exec();
    // Delete the view first: QML may still reference the objects above.
    view.reset();
    return result;
}
