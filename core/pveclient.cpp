// SPDX-License-Identifier: GPL-3.0-or-later
#include "pveclient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QStringList>
#include <QUrlQuery>

namespace {

const char *const UntrustedProperty = "pvedeckCertificateUntrusted";

QString messageFor(QNetworkReply *reply, int status, const QJsonDocument &doc)
{
    // Proxmox puts the error text in the HTTP reason phrase,
    // e.g. "401 authentication failure".
    QString message = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();

    // Parameter validation errors: {"errors": {"param": "message"}}
    if (doc.isObject()) {
        const QJsonObject errors = doc.object().value(QStringLiteral("errors")).toObject();
        QStringList details;
        for (QJsonObject::const_iterator it = errors.constBegin(); it != errors.constEnd(); ++it)
            details.append(it.key() + QStringLiteral(": ") + it.value().toString().trimmed());
        if (!details.isEmpty())
            message = details.join(QStringLiteral("; "));
    }

    if (message.isEmpty() && status == 0)
        message = reply->errorString();
    if (message.isEmpty())
        message = QStringLiteral("HTTP %1").arg(status);
    return message;
}

} // namespace

PveClient::PveClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_renewAfterMs(90 * 60 * 1000)
    , m_lifetimeMs(120 * 60 * 1000)
{
    m_renewTimer.setSingleShot(true);
    connect(&m_renewTimer, &QTimer::timeout, this, &PveClient::renewTicket);
}

PveClient::~PveClient()
{
    clearCredentials();
}

void PveClient::setEndpoint(const QString &host, int port, bool tls)
{
    if (m_host == host && m_port == port && m_tls == tls)
        return;
    m_host = host;
    m_port = port;
    m_tls = tls;
    emit endpointChanged();
}

QString PveClient::host() const
{
    return m_host;
}

bool PveClient::isTlsEnabled() const
{
    return m_tls;
}

PveClient::Headers PveClient::authHeaders() const
{
    Headers headers;
    if (m_authMethod == TokenAuth)
        headers.append(qMakePair(QByteArray("Authorization"),
                                 "PVEAPIToken=" + m_userId.toUtf8() + '=' + m_tokenSecret.toUtf8()));
    else if (!m_ticket.isEmpty())
        headers.append(qMakePair(QByteArray("Cookie"),
                                 "PVEAuthCookie=" + QUrl::toPercentEncoding(m_ticket)));
    return headers;
}

QUrl PveClient::baseUrl() const
{
    QUrl url;
    url.setScheme(m_tls ? QStringLiteral("https") : QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    return url;
}

void PveClient::setPinnedFingerprint(const QString &fingerprint)
{
    m_pinned = fingerprint.trimmed().toUpper();
}

QString PveClient::pinnedFingerprint() const
{
    return m_pinned;
}

void PveClient::setTicketTimings(int renewAfterMs, int lifetimeMs)
{
    m_renewAfterMs = renewAfterMs;
    m_lifetimeMs = lifetimeMs;
}

PveClient::State PveClient::state() const { return m_state; }
bool PveClient::isConnected() const { return m_state == Connected; }
PveClient::AuthMethod PveClient::authMethod() const { return m_authMethod; }
QString PveClient::userId() const { return m_userId; }
QString PveClient::serverVersion() const { return m_serverVersion; }
QString PveClient::errorString() const { return m_errorString; }

void PveClient::loginWithPassword(const QString &userId, const QString &password)
{
    clearCredentials();
    m_authMethod = PasswordAuth;
    m_userId = userId;
    m_password = password;
    setErrorString(QString());
    setState(Connecting);

    FormFields fields;
    fields << qMakePair(QStringLiteral("username"), userId)
           << qMakePair(QStringLiteral("password"), password);
    send(Post, QStringLiteral("/access/ticket"), fields, false, [this](const Reply &r) {
        handleTicket(r, false);
    });
}

void PveClient::loginWithToken(const QString &tokenId, const QString &secret)
{
    clearCredentials();
    m_authMethod = TokenAuth;
    m_userId = tokenId;
    m_tokenSecret = secret;
    setErrorString(QString());
    setState(Connecting);

    // A token has no login step; check it with a cheap call.
    send(Get, QStringLiteral("/version"), FormFields(), true, [this](const Reply &r) {
        if (m_state != Connecting)
            return;
        if (!r.ok) {
            const QString message = r.httpStatus == 401
                    ? tr("The API token was rejected") : r.error;
            setErrorString(message);
            setState(Disconnected);
            if (r.certificateUntrusted) {
                emit certificateUntrusted(m_untrustedFingerprint, m_untrustedSubject,
                                          m_untrustedIssuer, m_untrustedExpires, !m_pinned.isEmpty());
            } else {
                emit loginFailed(message, r.httpStatus);
            }
            return;
        }
        m_serverVersion = r.data.toObject().value(QStringLiteral("version")).toString();
        emit serverVersionChanged();
        setState(Connected);
        emit connected();
    });
}

void PveClient::submitTfa(const QString &response)
{
    if (m_state != AwaitingTfa)
        return;
    setState(Connecting);

    FormFields fields;
    fields << qMakePair(QStringLiteral("username"), m_userId)
           << qMakePair(QStringLiteral("tfa-challenge"), m_tfaChallenge)
           << qMakePair(QStringLiteral("password"), response);
    send(Post, QStringLiteral("/access/ticket"), fields, false, [this](const Reply &r) {
        if (m_state != Connecting)
            return;
        if (!r.ok) {
            // The challenge stays valid for a while, so let the user retry.
            const QString message = r.httpStatus == 401
                    ? tr("The code was not accepted") : r.error;
            setErrorString(message);
            setState(AwaitingTfa);
            emit tfaFailed(message);
            return;
        }
        handleTicket(r, false);
    });
}

void PveClient::retryLogin()
{
    // Copies: the login functions clear the members first.
    const QString user = m_userId;
    if (m_authMethod == TokenAuth && !m_tokenSecret.isEmpty()) {
        const QString secret = m_tokenSecret;
        loginWithToken(user, secret);
    } else if (m_authMethod == PasswordAuth && !m_password.isEmpty()) {
        const QString password = m_password;
        loginWithPassword(user, password);
    }
}

void PveClient::logout()
{
    // Proxmox has no logout call; tickets simply expire.
    clearCredentials();
    m_userId.clear();
    flushPending(false);
    setState(Disconnected);
}

void PveClient::renewTicket()
{
    if (m_authMethod != PasswordAuth || m_ticket.isEmpty() || m_renewing)
        return;
    if (m_ticketAge.isValid() && m_ticketAge.elapsed() >= m_lifetimeMs) {
        expire(tr("The session has expired"));
        return;
    }
    m_renewing = true;
    FormFields fields;
    fields << qMakePair(QStringLiteral("username"), m_userId)
           << qMakePair(QStringLiteral("password"), m_ticket);
    send(Post, QStringLiteral("/access/ticket"), fields, false, [this](const Reply &r) {
        m_renewing = false;
        if (m_state != Connected)
            return;
        handleTicket(r, true);
    });
}

void PveClient::get(const QString &path, Callback callback, const FormFields &query)
{
    call(Get, path, query, callback);
}

void PveClient::post(const QString &path, Callback callback, const FormFields &params)
{
    call(Post, path, params, callback);
}

void PveClient::put(const QString &path, Callback callback, const FormFields &params)
{
    call(Put, path, params, callback);
}

void PveClient::del(const QString &path, Callback callback, const FormFields &query)
{
    call(Delete, path, query, callback);
}

void PveClient::getPublic(const QString &path, Callback callback)
{
    send(Get, path, FormFields(), false, callback);
}

void PveClient::call(Method method, const QString &path, const FormFields &fields, Callback callback)
{
    if (m_state != Connected) {
        Reply r;
        r.error = tr("Not connected");
        if (callback)
            callback(r);
        return;
    }

    if (m_authMethod == PasswordAuth && m_ticketAge.isValid()) {
        const qint64 age = m_ticketAge.elapsed();
        if (age >= m_lifetimeMs) {
            // The device was probably asleep past the renewal time.
            Reply r;
            r.error = tr("The session has expired");
            r.httpStatus = 401;
            expire(r.error);
            if (callback)
                callback(r);
            return;
        }
        if (m_renewing || age >= m_renewAfterMs) {
            Pending p;
            p.method = method;
            p.path = path;
            p.fields = fields;
            p.callback = callback;
            m_pending.append(p);
            renewTicket();
            return;
        }
    }

    send(method, path, fields, true, [this, callback](const Reply &r) {
        if (r.httpStatus == 401 && m_state == Connected)
            expire(tr("The session has expired"));
        if (callback)
            callback(r);
    });
}

void PveClient::send(Method method, const QString &path, const FormFields &fields,
                     bool authenticated, Callback callback)
{
    QUrl url = baseUrl();
    url.setPath(QStringLiteral("/api2/json") + path);

    const bool hasBody = method == Post || method == Put;
    if (!hasBody && !fields.isEmpty())
        url.setQuery(QString::fromLatin1(Pve::formEncode(fields)), QUrl::StrictMode);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("pvedeck/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");

    if (authenticated) {
        if (m_authMethod == TokenAuth) {
            request.setRawHeader("Authorization",
                                 "PVEAPIToken=" + m_userId.toUtf8() + '=' + m_tokenSecret.toUtf8());
        } else if (!m_ticket.isEmpty()) {
            request.setRawHeader("Cookie", "PVEAuthCookie=" + QUrl::toPercentEncoding(m_ticket));
            if (method != Get)
                request.setRawHeader("CSRFPreventionToken", m_csrfToken.toUtf8());
        }
    }

    QNetworkReply *reply = nullptr;
    const QByteArray body = hasBody ? Pve::formEncode(fields) : QByteArray();
    if (hasBody)
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));

    switch (method) {
    case Get: reply = m_nam->get(request); break;
    case Post: reply = m_nam->post(request, body); break;
    case Put: reply = m_nam->put(request, body); break;
    case Delete: reply = m_nam->deleteResource(request); break;
    }

    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError> &errors) {
        handleSslErrors(reply, errors);
    });

    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();

        Reply r;
        r.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.certificateUntrusted = reply->property(UntrustedProperty).toBool();

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            r.data = doc.object().value(QStringLiteral("data"));

        r.ok = reply->error() == QNetworkReply::NoError
                && r.httpStatus >= 200 && r.httpStatus < 300;
        if (!r.ok) {
            r.error = r.certificateUntrusted
                    ? PveClient::tr("The server certificate is not trusted")
                    : messageFor(reply, r.httpStatus, doc);
        }
        if (callback)
            callback(r);
    });
}

void PveClient::handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
    const QString fingerprint = Pve::certificateFingerprint(cert);

    if (!fingerprint.isEmpty() && fingerprint == m_pinned) {
        reply->ignoreSslErrors(errors);
        return;
    }

    reply->setProperty(UntrustedProperty, true);
    m_untrustedFingerprint = fingerprint;
    m_untrustedSubject = cert.subjectInfo(QSslCertificate::CommonName).join(QStringLiteral(", "));
    m_untrustedIssuer = cert.issuerInfo(QSslCertificate::CommonName).join(QStringLiteral(", "));
    m_untrustedExpires = cert.expiryDate();
}

void PveClient::handleTicket(const Reply &r, bool renewal)
{
    // Login cancelled while the request was running.
    if (!renewal && m_state != Connecting)
        return;

    if (!r.ok) {
        if (renewal) {
            expire(tr("The session could not be renewed"));
            return;
        }
        const QString message = r.httpStatus == 401
                ? tr("Wrong user name or password") : r.error;
        // Keep the password if the certificate is the problem, so the
        // login can be retried once the user trusts it.
        if (!r.certificateUntrusted)
            m_password.clear();
        setErrorString(message);
        setState(Disconnected);
        if (r.certificateUntrusted) {
            emit certificateUntrusted(m_untrustedFingerprint, m_untrustedSubject,
                                      m_untrustedIssuer, m_untrustedExpires, !m_pinned.isEmpty());
        } else {
            emit loginFailed(message, r.httpStatus);
        }
        return;
    }

    const QJsonObject data = r.data.toObject();
    const QString ticket = data.value(QStringLiteral("ticket")).toString();
    const QJsonValue needTfa = data.value(QStringLiteral("NeedTFA"));

    if (!renewal && (needTfa.toInt() == 1 || needTfa.toBool())) {
        m_tfaChallenge = ticket;
        setState(AwaitingTfa);
        emit tfaRequired(ticket);
        return;
    }

    if (ticket.isEmpty()) {
        if (renewal) {
            expire(tr("The session could not be renewed"));
        } else {
            setErrorString(tr("Unexpected response from the server"));
            setState(Disconnected);
            emit loginFailed(m_errorString, r.httpStatus);
        }
        return;
    }

    m_ticket = ticket;
    m_csrfToken = data.value(QStringLiteral("CSRFPreventionToken")).toString();
    const QString username = data.value(QStringLiteral("username")).toString();
    if (!username.isEmpty())
        m_userId = username;
    m_password.clear();
    m_tfaChallenge.clear();
    m_ticketAge.start();
    m_renewTimer.start(m_renewAfterMs);

    if (renewal) {
        emit ticketRenewed();
        flushPending(true);
    } else {
        finishLogin();
    }
}

void PveClient::finishLogin()
{
    setState(Connected);
    emit connected();
    get(QStringLiteral("/version"), [this](const Reply &r) {
        if (!r.ok)
            return;
        m_serverVersion = r.data.toObject().value(QStringLiteral("version")).toString();
        emit serverVersionChanged();
    });
}

void PveClient::expire(const QString &message)
{
    if (m_state == Disconnected)
        return;
    clearCredentials();
    setErrorString(message);
    flushPending(false);
    setState(Disconnected);
    emit sessionExpired();
}

void PveClient::flushPending(bool ok)
{
    const QList<Pending> pending = m_pending;
    m_pending.clear();
    foreach (const Pending &p, pending) {
        if (ok) {
            call(p.method, p.path, p.fields, p.callback);
        } else if (p.callback) {
            Reply r;
            r.error = m_errorString.isEmpty() ? tr("Not connected") : m_errorString;
            p.callback(r);
        }
    }
}

void PveClient::clearCredentials()
{
    m_renewTimer.stop();
    m_ticketAge.invalidate();
    m_renewing = false;
    m_password.clear();
    m_tokenSecret.clear();
    m_tfaChallenge.clear();
    m_ticket.clear();
    m_csrfToken.clear();
    if (!m_serverVersion.isEmpty()) {
        m_serverVersion.clear();
        emit serverVersionChanged();
    }
}

void PveClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void PveClient::setErrorString(const QString &error)
{
    if (m_errorString == error)
        return;
    m_errorString = error;
    emit errorStringChanged();
}
