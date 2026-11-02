// SPDX-License-Identifier: GPL-3.0-or-later
#include "sailfishsecretbackend.h"

#include <QDebug>

#include <Sailfish/Secrets/createcollectionrequest.h>
#include <Sailfish/Secrets/deletesecretrequest.h>
#include <Sailfish/Secrets/result.h>
#include <Sailfish/Secrets/secret.h>
#include <Sailfish/Secrets/storedsecretrequest.h>
#include <Sailfish/Secrets/storesecretrequest.h>

using namespace Sailfish::Secrets;

namespace {

const QString CollectionName = QStringLiteral("pvedeck");

// Runs onFinished once the request is done, then deletes the request.
template <typename R>
void whenFinished(R *request, std::function<void(R *)> onFinished)
{
    QObject::connect(request, &Request::statusChanged, request, [request, onFinished]() {
        if (request->status() != Request::Finished)
            return;
        onFinished(request);
        request->deleteLater();
    });
    request->startRequest();
}

bool succeeded(const Result &result)
{
    return result.code() == Result::Succeeded;
}

} // namespace

SailfishSecretBackend::SailfishSecretBackend(QObject *parent)
    : SecretBackend(parent)
{
}

Secret::Identifier SailfishSecretBackend::identifier(const QString &key) const
{
    return Secret::Identifier(key, CollectionName, SecretManager::DefaultEncryptedStoragePluginName);
}

void SailfishSecretBackend::ensureCollection(std::function<void(bool)> next)
{
    if (m_collectionState == Ready) {
        next(true);
        return;
    }
    m_waitingForCollection.append(next);
    if (m_collectionState == Creating)
        return;
    m_collectionState = Creating;

    CreateCollectionRequest *request = new CreateCollectionRequest(this);
    request->setManager(&m_manager);
    request->setCollectionName(CollectionName);
    request->setAccessControlMode(SecretManager::OwnerOnlyMode);
    request->setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request->setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request->setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request->setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request->setUserInteractionMode(SecretManager::SystemInteraction);

    whenFinished<CreateCollectionRequest>(request, [this](CreateCollectionRequest *r) {
        const Result result = r->result();
        const bool ok = succeeded(result)
                || result.errorCode() == Result::CollectionAlreadyExistsError;
        if (!ok)
            qWarning() << "Could not create secrets collection:" << result.errorMessage();
        m_collectionState = ok ? Ready : Unknown;
        const QList<std::function<void(bool)> > waiting = m_waitingForCollection;
        m_waitingForCollection.clear();
        foreach (const std::function<void(bool)> &fn, waiting)
            fn(ok);
    });
}

void SailfishSecretBackend::store(const QString &key, const QByteArray &data, DoneCallback done)
{
    ensureCollection([this, key, data, done](bool ok) {
        if (!ok) {
            if (done) done(false);
            return;
        }
        storeNew(key, data, done, false);
    });
}

void SailfishSecretBackend::storeNew(const QString &key, const QByteArray &data,
                                     DoneCallback done, bool retried)
{
    Secret secret(identifier(key));
    secret.setType(Secret::TypeBlob);
    secret.setData(data);

    StoreSecretRequest *request = new StoreSecretRequest(this);
    request->setManager(&m_manager);
    request->setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request->setUserInteractionMode(SecretManager::SystemInteraction);
    request->setSecret(secret);

    whenFinished<StoreSecretRequest>(request, [this, key, data, done, retried](StoreSecretRequest *r) {
        const Result result = r->result();
        if (succeeded(result)) {
            if (done) done(true);
            return;
        }
        if (!retried && result.errorCode() == Result::SecretAlreadyExistsError) {
            // Overwrite: delete the old value, then store again.
            remove(key, [this, key, data, done](bool) { storeNew(key, data, done, true); });
            return;
        }
        qWarning() << "Could not store secret:" << result.errorMessage();
        if (done) done(false);
    });
}

void SailfishSecretBackend::load(const QString &key, LoadCallback done)
{
    ensureCollection([this, key, done](bool ok) {
        if (!ok) {
            if (done) done(false, false, QByteArray());
            return;
        }
        StoredSecretRequest *request = new StoredSecretRequest(this);
        request->setManager(&m_manager);
        request->setIdentifier(identifier(key));
        request->setUserInteractionMode(SecretManager::SystemInteraction);

        whenFinished<StoredSecretRequest>(request, [done](StoredSecretRequest *r) {
            const Result result = r->result();
            if (succeeded(result)) {
                if (done) done(true, true, r->secret().data());
                return;
            }
            const bool missing = result.errorCode() == Result::InvalidSecretError
                    || result.errorCode() == Result::InvalidSecretIdentifierError;
            if (!missing)
                qWarning() << "Could not read secret:" << result.errorMessage();
            if (done) done(missing, false, QByteArray());
        });
    });
}

void SailfishSecretBackend::remove(const QString &key, DoneCallback done)
{
    ensureCollection([this, key, done](bool ok) {
        if (!ok) {
            if (done) done(false);
            return;
        }
        DeleteSecretRequest *request = new DeleteSecretRequest(this);
        request->setManager(&m_manager);
        request->setIdentifier(identifier(key));
        request->setUserInteractionMode(SecretManager::SystemInteraction);

        whenFinished<DeleteSecretRequest>(request, [done](DeleteSecretRequest *r) {
            const Result result = r->result();
            const bool ok = succeeded(result)
                    || result.errorCode() == Result::InvalidSecretError
                    || result.errorCode() == Result::InvalidSecretIdentifierError;
            if (done) done(ok);
        });
    });
}
