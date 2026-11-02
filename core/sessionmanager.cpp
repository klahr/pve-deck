// SPDX-License-Identifier: GPL-3.0-or-later
#include "sessionmanager.h"

#include <QPointer>
#include <QQmlEngine>

#include "pveclient.h"
#include "secretbackend.h"
#include "serverstore.h"
#include "tfachallenge.h"

SessionManager::SessionManager(ServerStore *store, SecretBackend *secrets, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_secrets(secrets)
{
}

QStringList SessionManager::connectedIds() const
{
    return m_connectedIds;
}

QStringList SessionManager::busyIds() const
{
    return m_busy.toList();
}

void SessionManager::setTlsEnabled(bool enabled)
{
    m_tls = enabled;
}

PveClient *SessionManager::client(const QString &profileId)
{
    if (PveClient *existing = m_clients.value(profileId))
        return existing;
    if (!m_store->contains(profileId))
        return nullptr;

    PveClient *c = new PveClient(this);
    QQmlEngine::setObjectOwnership(c, QQmlEngine::CppOwnership);
    m_clients.insert(profileId, c);

    const QString id = profileId;
    connect(c, &PveClient::stateChanged, this, [this, id]() {
        updateConnectedIds();
        PveClient *cl = m_clients.value(id);
        if (cl && cl->state() != PveClient::Connecting)
            setBusy(id, false);
        if (cl && cl->state() == PveClient::Connecting)
            setBusy(id, true);
    });
    connect(c, &PveClient::connected, this, [this, id]() { onConnected(id); });
    connect(c, &PveClient::loginFailed, this, [this, id](const QString &message, int status) {
        onLoginFailed(id, message, status);
    });
    connect(c, &PveClient::tfaRequired, this, [this, id](const QString &ticket) {
        QVariantMap challenge = TfaChallenge::parse(ticket).toVariantMap();
        const QStringList methods = challenge.value(QStringLiteral("methods")).toStringList();
        const QString last = m_store->profile(id).lastTfaMethod;
        challenge.insert(QStringLiteral("defaultMethod"),
                         methods.contains(last) ? last : methods.value(0));
        m_tfaChallenges.insert(id, challenge);
        emit tfaRequired(id);
    });
    connect(c, &PveClient::tfaFailed, this, [this, id](const QString &message) {
        emit tfaFailed(id, message);
    });
    connect(c, &PveClient::certificateUntrusted, this,
            [this, id](const QString &fp, const QString &subject, const QString &issuer,
                       const QDateTime &expires, bool mismatch) {
        emit certificateUntrusted(id, fp, subject, issuer, expires, mismatch);
    });
    connect(c, &PveClient::sessionExpired, this, [this, id]() {
        PveClient *cl = m_clients.value(id);
        emit sessionExpired(id, cl ? cl->errorString() : QString());
    });
    return c;
}

bool SessionManager::isConnected(const QString &profileId) const
{
    PveClient *c = m_clients.value(profileId);
    return c && c->isConnected();
}

void SessionManager::connectServer(const QString &profileId)
{
    PveClient *c = client(profileId);
    if (!c || c->state() != PveClient::Disconnected)
        return;

    const ServerProfile profile = m_store->profile(profileId);
    if (!profile.rememberSecret && profile.authMethod == ServerProfile::PasswordAuth) {
        emit secretRequired(profileId, QString());
        return;
    }

    setBusy(profileId, true);
    QPointer<SessionManager> self(this);
    m_secrets->load(profile.secretKey(), [self, profileId](bool ok, bool found, const QByteArray &data) {
        if (!self)
            return;
        self->setBusy(profileId, false);
        if (ok && found && !data.isEmpty())
            self->startLogin(profileId, QString::fromUtf8(data), true);
        else
            emit self->secretRequired(profileId, QString());
    });
}

void SessionManager::login(const QString &profileId, const QString &secret, bool remember)
{
    if (!m_store->contains(profileId))
        return;
    m_store->setRememberSecret(profileId, remember);
    if (!remember)
        forgetSecret(profileId);

    PendingLogin pending;
    pending.secret = secret;
    pending.remember = remember;
    m_pending.insert(profileId, pending);
    startLogin(profileId, secret, false);
}

void SessionManager::startLogin(const QString &profileId, const QString &secret, bool fromStore)
{
    PveClient *c = client(profileId);
    if (!c)
        return;

    PendingLogin pending = m_pending.value(profileId);
    pending.secret = secret;
    pending.fromStore = fromStore;
    m_pending.insert(profileId, pending);

    const ServerProfile profile = m_store->profile(profileId);
    c->setEndpoint(profile.host, profile.port, m_tls);
    c->setPinnedFingerprint(profile.fingerprint);

    if (profile.authMethod == ServerProfile::TokenAuth)
        c->loginWithToken(profile.tokenId(), secret);
    else
        c->loginWithPassword(profile.userId(), secret);
}

void SessionManager::submitTfa(const QString &profileId, const QString &method, const QString &code)
{
    PveClient *c = m_clients.value(profileId);
    if (!c)
        return;
    if (m_pending.contains(profileId))
        m_pending[profileId].tfaMethod = method;
    c->submitTfa(method + QLatin1Char(':') + TfaChallenge::normalize(method, code));
}

QVariantMap SessionManager::tfaChallenge(const QString &profileId) const
{
    return m_tfaChallenges.value(profileId);
}

bool SessionManager::isValidTfaCode(const QString &method, const QString &code) const
{
    return TfaChallenge::isValid(method, code);
}

void SessionManager::trustCertificate(const QString &profileId, const QString &fingerprint)
{
    m_store->setFingerprint(profileId, fingerprint);
    PveClient *c = m_clients.value(profileId);
    if (!c)
        return;
    c->setPinnedFingerprint(fingerprint);
    if (m_pending.contains(profileId))
        c->retryLogin();
}

void SessionManager::cancelLogin(const QString &profileId)
{
    m_pending.remove(profileId);
    m_tfaChallenges.remove(profileId);
    PveClient *c = m_clients.value(profileId);
    if (c && !c->isConnected())
        c->logout();
    setBusy(profileId, false);
}

void SessionManager::logout(const QString &profileId)
{
    m_pending.remove(profileId);
    m_tfaChallenges.remove(profileId);
    if (PveClient *c = m_clients.value(profileId))
        c->logout();
}

void SessionManager::logoutAll()
{
    foreach (const QString &id, m_clients.keys())
        logout(id);
}

void SessionManager::storeSecret(const QString &profileId, const QString &secret)
{
    const ServerProfile profile = m_store->profile(profileId);
    if (profile.id.isEmpty() || secret.isEmpty())
        return;
    m_secrets->store(profile.secretKey(), secret.toUtf8(), SecretBackend::DoneCallback());
}

void SessionManager::forgetSecret(const QString &profileId)
{
    const ServerProfile profile = m_store->profile(profileId);
    if (profile.id.isEmpty())
        return;
    m_secrets->remove(profile.secretKey(), SecretBackend::DoneCallback());
}

void SessionManager::removeServer(const QString &profileId)
{
    logout(profileId);
    forgetSecret(profileId);
    if (PveClient *c = m_clients.take(profileId))
        c->deleteLater();
    setBusy(profileId, false);
    m_store->remove(profileId);
    updateConnectedIds();
}

void SessionManager::onConnected(const QString &profileId)
{
    const PendingLogin pending = m_pending.take(profileId);
    m_tfaChallenges.remove(profileId);
    if (pending.remember && !pending.fromStore)
        storeSecret(profileId, pending.secret);
    if (!pending.tfaMethod.isEmpty())
        m_store->setLastTfaMethod(profileId, pending.tfaMethod);
    emit connected(profileId);
}

void SessionManager::onLoginFailed(const QString &profileId, const QString &message, int httpStatus)
{
    const PendingLogin pending = m_pending.take(profileId);
    m_tfaChallenges.remove(profileId);
    if (pending.fromStore && httpStatus == 401) {
        // The saved password or token no longer works; ask for a new one.
        forgetSecret(profileId);
        emit secretRequired(profileId, message);
        return;
    }
    emit loginFailed(profileId, message);
}

void SessionManager::setBusy(const QString &profileId, bool busy)
{
    const bool changed = busy ? !m_busy.contains(profileId) : m_busy.contains(profileId);
    if (!changed)
        return;
    if (busy)
        m_busy.insert(profileId);
    else
        m_busy.remove(profileId);
    emit busyIdsChanged();
}

void SessionManager::updateConnectedIds()
{
    QStringList ids;
    for (QHash<QString, PveClient *>::const_iterator it = m_clients.constBegin();
         it != m_clients.constEnd(); ++it) {
        if (it.value()->isConnected())
            ids.append(it.key());
    }
    ids.sort();
    if (ids != m_connectedIds) {
        m_connectedIds = ids;
        emit connectedIdsChanged();
    }
}
