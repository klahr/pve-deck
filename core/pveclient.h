// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_PVECLIENT_H
#define PVEDECK_PVECLIENT_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>

#include "formencoding.h"

class QNetworkAccessManager;
class QNetworkReply;
class QSslError;

// One authenticated connection to a Proxmox VE API endpoint.
//
// Password login:  POST /access/ticket -> ticket + CSRF token.
//   If the user has 2FA, the response has NeedTFA=1 and a partial
//   ticket; submitTfa("totp:123456") completes the login.
//   The ticket is valid for 2 hours and is renewed automatically by
//   posting the current ticket as the password.
// Token login: Authorization: PVEAPIToken=user@realm!name=secret.
//
// Self-signed certificates are accepted only if their SHA-256
// fingerprint matches the pinned one; otherwise the request fails and
// certificateUntrusted() is emitted so the user can decide.
class PveClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY stateChanged)
    Q_PROPERTY(QString host READ host NOTIFY endpointChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverVersionChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    enum State {
        Disconnected,
        Connecting,
        AwaitingTfa,
        Connected
    };
    Q_ENUM(State)

    enum AuthMethod {
        PasswordAuth,
        TokenAuth
    };

    struct Reply {
        bool ok = false;
        int httpStatus = 0;
        QJsonValue data;
        QString error;
        bool certificateUntrusted = false;
    };
    typedef std::function<void(const Reply &)> Callback;

    explicit PveClient(QObject *parent = nullptr);
    ~PveClient();

    // tls=false is only meant for unit tests.
    void setEndpoint(const QString &host, int port, bool tls = true);
    QString host() const;
    QUrl baseUrl() const;

    void setPinnedFingerprint(const QString &fingerprint);
    QString pinnedFingerprint() const;

    // Timings, adjustable for tests. Defaults: renew after 90 min,
    // ticket lifetime 120 min.
    void setTicketTimings(int renewAfterMs, int lifetimeMs);

    State state() const;
    bool isConnected() const;
    AuthMethod authMethod() const;
    QString userId() const;
    QString serverVersion() const;
    QString errorString() const;

    Q_INVOKABLE void loginWithPassword(const QString &userId, const QString &password);
    Q_INVOKABLE void loginWithToken(const QString &tokenId, const QString &secret);
    // response is "<type>:<value>", e.g. "totp:123456" or "recovery:abcd-...".
    Q_INVOKABLE void submitTfa(const QString &response);
    // Repeat the last login attempt, e.g. after the certificate was trusted.
    Q_INVOKABLE void retryLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void renewTicket();

    // Authenticated API calls. path is relative to /api2/json, e.g. "/nodes".
    void get(const QString &path, Callback callback, const FormFields &query = FormFields());
    void post(const QString &path, Callback callback, const FormFields &params = FormFields());
    void put(const QString &path, Callback callback, const FormFields &params = FormFields());
    void del(const QString &path, Callback callback, const FormFields &query = FormFields());

    // Unauthenticated GET, e.g. /access/domains before login.
    void getPublic(const QString &path, Callback callback);

signals:
    void stateChanged();
    void endpointChanged();
    void serverVersionChanged();
    void errorStringChanged();
    void connected();
    void loginFailed(const QString &message, int httpStatus);
    void tfaRequired(const QString &challenge);
    void tfaFailed(const QString &message);
    void certificateUntrusted(const QString &fingerprint, const QString &subject,
                              const QString &issuer, const QDateTime &expires, bool mismatch);
    void ticketRenewed();
    void sessionExpired();

private:
    enum Method { Get, Post, Put, Delete };

    struct Pending {
        Method method;
        QString path;
        FormFields fields;
        Callback callback;
    };

    void send(Method method, const QString &path, const FormFields &fields,
              bool authenticated, Callback callback);
    void call(Method method, const QString &path, const FormFields &fields, Callback callback);
    void handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors);
    void handleTicket(const Reply &reply, bool renewal);
    void finishLogin();
    void expire(const QString &message);
    void flushPending(bool ok);
    void clearCredentials();
    void setState(State state);
    void setErrorString(const QString &error);

    QNetworkAccessManager *m_nam;
    QString m_host;
    int m_port = 8006;
    bool m_tls = true;
    QString m_pinned;

    State m_state = Disconnected;
    AuthMethod m_authMethod = PasswordAuth;
    QString m_userId;
    QString m_password;      // kept only while logging in
    QString m_tokenSecret;
    QString m_tfaChallenge;
    QString m_ticket;
    QString m_csrfToken;
    QString m_serverVersion;
    QString m_errorString;

    QElapsedTimer m_ticketAge;
    QTimer m_renewTimer;
    int m_renewAfterMs;
    int m_lifetimeMs;
    bool m_renewing = false;
    QList<Pending> m_pending;

    // Certificate seen in the last failed handshake.
    QString m_untrustedFingerprint;
    QString m_untrustedSubject;
    QString m_untrustedIssuer;
    QDateTime m_untrustedExpires;
};

#endif // PVEDECK_PVECLIENT_H
