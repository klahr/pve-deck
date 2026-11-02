// SPDX-License-Identifier: GPL-3.0-or-later
#include "serverstore.h"

#include <QSettings>
#include <QUuid>

ServerStore::ServerStore(QSettings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings ? settings : new QSettings)
{
    load();
}

ServerStore::~ServerStore()
{
    delete m_settings;
}

int ServerStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_profiles.size();
}

QVariant ServerStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profiles.size())
        return QVariant();
    const ServerProfile &p = m_profiles.at(index.row());
    switch (role) {
    case ProfileIdRole: return p.id;
    case NameRole: return p.name;
    case DisplayNameRole: return p.displayName();
    case HostRole: return p.host;
    case PortRole: return p.port;
    case UsernameRole: return p.username;
    case RealmRole: return p.realm;
    case UserIdRole: return p.userId();
    case AuthMethodRole: return ServerProfile::authMethodToString(p.authMethod);
    case TokenNameRole: return p.tokenName;
    case FingerprintRole: return p.fingerprint;
    case RememberSecretRole: return p.rememberSecret;
    }
    return QVariant();
}

QHash<int, QByteArray> ServerStore::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ProfileIdRole] = "profileId";
    roles[NameRole] = "name";
    roles[DisplayNameRole] = "displayName";
    roles[HostRole] = "host";
    roles[PortRole] = "port";
    roles[UsernameRole] = "username";
    roles[RealmRole] = "realm";
    roles[UserIdRole] = "userId";
    roles[AuthMethodRole] = "authMethod";
    roles[TokenNameRole] = "tokenName";
    roles[FingerprintRole] = "fingerprint";
    roles[RememberSecretRole] = "rememberSecret";
    return roles;
}

int ServerStore::count() const
{
    return m_profiles.size();
}

QString ServerStore::add(const QVariantMap &data)
{
    ServerProfile p = ServerProfile::fromVariantMap(data);
    p.id = QUuid::createUuid().toString();
    if (!p.isValid())
        return QString();
    beginInsertRows(QModelIndex(), m_profiles.size(), m_profiles.size());
    m_profiles.append(p);
    endInsertRows();
    save();
    emit countChanged();
    return p.id;
}

bool ServerStore::update(const QString &profileId, const QVariantMap &data)
{
    const int row = indexOf(profileId);
    if (row < 0)
        return false;
    const ServerProfile old = m_profiles.at(row);
    ServerProfile p = ServerProfile::fromVariantMap(data);
    p.id = profileId;
    if (!p.isValid())
        return false;
    // A different host means a different certificate.
    if (p.host == old.host && p.port == old.port && !data.contains(QStringLiteral("fingerprint")))
        p.fingerprint = old.fingerprint;
    if (!data.contains(QStringLiteral("lastTfaMethod")))
        p.lastTfaMethod = old.lastTfaMethod;
    replaceAt(row, p);
    return true;
}

bool ServerStore::remove(const QString &profileId)
{
    const int row = indexOf(profileId);
    if (row < 0)
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_profiles.removeAt(row);
    endRemoveRows();
    save();
    emit countChanged();
    return true;
}

QVariantMap ServerStore::get(const QString &profileId) const
{
    const int row = indexOf(profileId);
    return row < 0 ? QVariantMap() : m_profiles.at(row).toVariantMap();
}

void ServerStore::setFingerprint(const QString &profileId, const QString &fingerprint)
{
    const int row = indexOf(profileId);
    if (row < 0)
        return;
    ServerProfile p = m_profiles.at(row);
    p.fingerprint = fingerprint;
    replaceAt(row, p);
}

void ServerStore::setRememberSecret(const QString &profileId, bool remember)
{
    const int row = indexOf(profileId);
    if (row < 0 || m_profiles.at(row).rememberSecret == remember)
        return;
    ServerProfile p = m_profiles.at(row);
    p.rememberSecret = remember;
    replaceAt(row, p);
}

void ServerStore::setLastTfaMethod(const QString &profileId, const QString &method)
{
    const int row = indexOf(profileId);
    if (row < 0 || m_profiles.at(row).lastTfaMethod == method)
        return;
    ServerProfile p = m_profiles.at(row);
    p.lastTfaMethod = method;
    replaceAt(row, p);
}

bool ServerStore::contains(const QString &profileId) const
{
    return indexOf(profileId) >= 0;
}

ServerProfile ServerStore::profile(const QString &profileId) const
{
    const int row = indexOf(profileId);
    return row < 0 ? ServerProfile() : m_profiles.at(row);
}

int ServerStore::indexOf(const QString &profileId) const
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == profileId)
            return i;
    }
    return -1;
}

void ServerStore::replaceAt(int row, const ServerProfile &profile)
{
    m_profiles[row] = profile;
    save();
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
    emit profileChanged(profile.id);
}

void ServerStore::load()
{
    m_profiles.clear();
    const int size = m_settings->beginReadArray(QStringLiteral("servers"));
    for (int i = 0; i < size; ++i) {
        m_settings->setArrayIndex(i);
        QVariantMap map;
        foreach (const QString &key, m_settings->childKeys())
            map.insert(key, m_settings->value(key));
        ServerProfile p = ServerProfile::fromVariantMap(map);
        if (!p.id.isEmpty())
            m_profiles.append(p);
    }
    m_settings->endArray();
}

void ServerStore::save()
{
    m_settings->remove(QStringLiteral("servers"));
    m_settings->beginWriteArray(QStringLiteral("servers"), m_profiles.size());
    for (int i = 0; i < m_profiles.size(); ++i) {
        m_settings->setArrayIndex(i);
        QVariantMap map = m_profiles.at(i).toVariantMap();
        // Derived values are not stored.
        map.remove(QStringLiteral("userId"));
        map.remove(QStringLiteral("displayName"));
        for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it)
            m_settings->setValue(it.key(), it.value());
    }
    m_settings->endArray();
    m_settings->sync();
}
