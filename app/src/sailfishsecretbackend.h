// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_SAILFISHSECRETBACKEND_H
#define PVEDECK_SAILFISHSECRETBACKEND_H

#include <QList>

#include <Sailfish/Secrets/secretmanager.h>

#include "secretbackend.h"

// Secrets stored with Sailfish Secrets in an app-owned collection that
// is encrypted and unlocked together with the device lock.
class SailfishSecretBackend : public SecretBackend
{
    Q_OBJECT

public:
    explicit SailfishSecretBackend(QObject *parent = nullptr);

    void store(const QString &key, const QByteArray &data, DoneCallback done) override;
    void load(const QString &key, LoadCallback done) override;
    void remove(const QString &key, DoneCallback done) override;

private:
    void ensureCollection(std::function<void(bool ok)> next);
    void storeNew(const QString &key, const QByteArray &data, DoneCallback done, bool retried);
    Sailfish::Secrets::Secret::Identifier identifier(const QString &key) const;

    Sailfish::Secrets::SecretManager m_manager;
    enum CollectionState { Unknown, Creating, Ready };
    CollectionState m_collectionState = Unknown;
    QList<std::function<void(bool)> > m_waitingForCollection;
};

#endif // PVEDECK_SAILFISHSECRETBACKEND_H
