// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_SECRETBACKEND_H
#define PVEDECK_SECRETBACKEND_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

// Asynchronous key/value store for secrets (passwords, API token secrets,
// SSH keys). The app uses Sailfish Secrets; tests use MemorySecretBackend.
class SecretBackend : public QObject
{
    Q_OBJECT

public:
    // found is false when the key does not exist; ok is false on errors.
    typedef std::function<void(bool ok, bool found, const QByteArray &data)> LoadCallback;
    typedef std::function<void(bool ok)> DoneCallback;

    explicit SecretBackend(QObject *parent = nullptr) : QObject(parent) {}

    virtual void store(const QString &key, const QByteArray &data, DoneCallback done) = 0;
    virtual void load(const QString &key, LoadCallback done) = 0;
    virtual void remove(const QString &key, DoneCallback done) = 0;
};

#endif // PVEDECK_SECRETBACKEND_H
