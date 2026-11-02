// SPDX-License-Identifier: GPL-3.0-or-later
#include "serverprofile.h"

#include <QUrl>

QString ServerProfile::userId() const
{
    if (username.contains(QLatin1Char('@')) || realm.isEmpty())
        return username;
    return username + QLatin1Char('@') + realm;
}

QString ServerProfile::tokenId() const
{
    return userId() + QLatin1Char('!') + tokenName;
}

QString ServerProfile::displayName() const
{
    return name.trimmed().isEmpty() ? host : name.trimmed();
}

bool ServerProfile::isValid() const
{
    if (host.isEmpty() || username.isEmpty() || port <= 0 || port > 65535)
        return false;
    if (authMethod == TokenAuth && tokenName.isEmpty())
        return false;
    return true;
}

void ServerProfile::setHostInput(const QString &input)
{
    QString text = input.trimmed();
    if (text.isEmpty()) {
        host.clear();
        return;
    }
    if (!text.contains(QStringLiteral("://")))
        text.prepend(QStringLiteral("https://"));
    const QUrl url(text);
    if (url.isValid() && !url.host().isEmpty()) {
        host = url.host();
        if (url.port() > 0)
            port = url.port();
    } else {
        host = input.trimmed();
    }
}

QString ServerProfile::secretKey() const
{
    QString key = id;
    key.remove(QLatin1Char('{'));
    key.remove(QLatin1Char('}'));
    key.remove(QLatin1Char('-'));
    return QStringLiteral("server") + key;
}

QVariantMap ServerProfile::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("profileId"), id);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("host"), host);
    map.insert(QStringLiteral("port"), port);
    map.insert(QStringLiteral("username"), username);
    map.insert(QStringLiteral("realm"), realm);
    map.insert(QStringLiteral("authMethod"), authMethodToString(authMethod));
    map.insert(QStringLiteral("tokenName"), tokenName);
    map.insert(QStringLiteral("fingerprint"), fingerprint);
    map.insert(QStringLiteral("rememberSecret"), rememberSecret);
    map.insert(QStringLiteral("lastTfaMethod"), lastTfaMethod);
    map.insert(QStringLiteral("userId"), userId());
    map.insert(QStringLiteral("displayName"), displayName());
    return map;
}

ServerProfile ServerProfile::fromVariantMap(const QVariantMap &map)
{
    ServerProfile p;
    p.id = map.value(QStringLiteral("profileId")).toString();
    p.name = map.value(QStringLiteral("name")).toString().trimmed();
    if (map.contains(QStringLiteral("port")))
        p.port = map.value(QStringLiteral("port")).toInt();
    p.setHostInput(map.value(QStringLiteral("host")).toString());
    if (map.contains(QStringLiteral("username")))
        p.username = map.value(QStringLiteral("username")).toString().trimmed();
    if (map.contains(QStringLiteral("realm")))
        p.realm = map.value(QStringLiteral("realm")).toString().trimmed();
    p.authMethod = authMethodFromString(map.value(QStringLiteral("authMethod")).toString());
    p.tokenName = map.value(QStringLiteral("tokenName")).toString().trimmed();
    p.fingerprint = map.value(QStringLiteral("fingerprint")).toString();
    if (map.contains(QStringLiteral("rememberSecret")))
        p.rememberSecret = map.value(QStringLiteral("rememberSecret")).toBool();
    p.lastTfaMethod = map.value(QStringLiteral("lastTfaMethod")).toString();
    return p;
}

QString ServerProfile::authMethodToString(AuthMethod method)
{
    return method == TokenAuth ? QStringLiteral("token") : QStringLiteral("password");
}

ServerProfile::AuthMethod ServerProfile::authMethodFromString(const QString &value)
{
    return value == QLatin1String("token") ? TokenAuth : PasswordAuth;
}
