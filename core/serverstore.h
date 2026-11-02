// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_SERVERSTORE_H
#define PVEDECK_SERVERSTORE_H

#include <QAbstractListModel>
#include <QList>

#include "serverprofile.h"

class QSettings;

// List of server profiles, persisted in QSettings.
class ServerStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        ProfileIdRole = Qt::UserRole + 1,
        NameRole,
        DisplayNameRole,
        HostRole,
        PortRole,
        UsernameRole,
        RealmRole,
        UserIdRole,
        AuthMethodRole,
        TokenNameRole,
        FingerprintRole,
        RememberSecretRole
    };

    // Takes ownership of settings if given; otherwise uses QSettings().
    explicit ServerStore(QSettings *settings = nullptr, QObject *parent = nullptr);
    ~ServerStore();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    // Returns the new profile id, or an empty string if the data is invalid.
    Q_INVOKABLE QString add(const QVariantMap &data);
    Q_INVOKABLE bool update(const QString &profileId, const QVariantMap &data);
    Q_INVOKABLE bool remove(const QString &profileId);
    Q_INVOKABLE QVariantMap get(const QString &profileId) const;
    Q_INVOKABLE void setFingerprint(const QString &profileId, const QString &fingerprint);
    Q_INVOKABLE void setRememberSecret(const QString &profileId, bool remember);
    void setLastTfaMethod(const QString &profileId, const QString &method);

    bool contains(const QString &profileId) const;
    ServerProfile profile(const QString &profileId) const;

signals:
    void countChanged();
    void profileChanged(const QString &profileId);

private:
    int indexOf(const QString &profileId) const;
    void load();
    void save();
    void replaceAt(int row, const ServerProfile &profile);

    QSettings *m_settings;
    QList<ServerProfile> m_profiles;
};

#endif // PVEDECK_SERVERSTORE_H
