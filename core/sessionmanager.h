// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_SESSIONMANAGER_H
#define PVEDECK_SESSIONMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class PveClient;
class SecretBackend;
class ServerStore;

// Owns one PveClient per server profile, so several servers can be
// logged in at the same time. Drives the login flow for the UI:
//
//   connectServer(id)
//     -> stored secret?  yes: log in with it
//                        no:  secretRequired(id)  -> login(id, secret, remember)
//     -> certificateUntrusted(...)                 -> trustCertificate(id, fp)
//     -> tfaRequired(id)                           -> submitTfa(id, type, code)
//     -> connected(id) | loginFailed(id, message)
class SessionManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList connectedIds READ connectedIds NOTIFY connectedIdsChanged)
    Q_PROPERTY(QStringList busyIds READ busyIds NOTIFY busyIdsChanged)

public:
    SessionManager(ServerStore *store, SecretBackend *secrets, QObject *parent = nullptr);

    QStringList connectedIds() const;
    QStringList busyIds() const;

    // Unit tests only: talk plain HTTP to a mock server.
    void setTlsEnabled(bool enabled);

    // Created on demand; owned by the SessionManager.
    Q_INVOKABLE PveClient *client(const QString &profileId);

    Q_INVOKABLE bool isConnected(const QString &profileId) const;
    Q_INVOKABLE void connectServer(const QString &profileId);
    // secret is the password or, for token profiles, the token secret.
    Q_INVOKABLE void login(const QString &profileId, const QString &secret, bool remember);
    // method: "totp", "yubico" or "recovery"
    Q_INVOKABLE void submitTfa(const QString &profileId, const QString &method, const QString &code);
    // The pending challenge: {methods, recoveryIds, webauthn, usable, defaultMethod}
    Q_INVOKABLE QVariantMap tfaChallenge(const QString &profileId) const;
    Q_INVOKABLE bool isValidTfaCode(const QString &method, const QString &code) const;
    Q_INVOKABLE void trustCertificate(const QString &profileId, const QString &fingerprint);
    Q_INVOKABLE void cancelLogin(const QString &profileId);
    Q_INVOKABLE void logout(const QString &profileId);
    Q_INVOKABLE void logoutAll();

    Q_INVOKABLE void storeSecret(const QString &profileId, const QString &secret);
    Q_INVOKABLE void forgetSecret(const QString &profileId);
    // Logs out, deletes the stored secret and removes the profile.
    Q_INVOKABLE void removeServer(const QString &profileId);

signals:
    void connectedIdsChanged();
    void busyIdsChanged();

    void secretRequired(const QString &profileId, const QString &message);
    void tfaRequired(const QString &profileId);
    void tfaFailed(const QString &profileId, const QString &message);
    void certificateUntrusted(const QString &profileId, const QString &fingerprint,
                              const QString &subject, const QString &issuer,
                              const QDateTime &expires, bool mismatch);
    void connected(const QString &profileId);
    void loginFailed(const QString &profileId, const QString &message);
    void sessionExpired(const QString &profileId, const QString &message);

private:
    struct PendingLogin {
        QString secret;
        bool remember = false;
        bool fromStore = false;
        QString tfaMethod;
    };

    void startLogin(const QString &profileId, const QString &secret, bool fromStore);
    void onConnected(const QString &profileId);
    void onLoginFailed(const QString &profileId, const QString &message, int httpStatus);
    void setBusy(const QString &profileId, bool busy);
    void updateConnectedIds();

    ServerStore *m_store;
    SecretBackend *m_secrets;
    QHash<QString, PveClient *> m_clients;
    QHash<QString, PendingLogin> m_pending;
    QHash<QString, QVariantMap> m_tfaChallenges;
    QSet<QString> m_busy;
    QStringList m_connectedIds;
    bool m_tls = true;
};

#endif // PVEDECK_SESSIONMANAGER_H
