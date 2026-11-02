// SPDX-License-Identifier: GPL-3.0-or-later
#include "memorysecretbackend.h"

#include <QTimer>

MemorySecretBackend::MemorySecretBackend(QObject *parent)
    : SecretBackend(parent)
{
}

void MemorySecretBackend::store(const QString &key, const QByteArray &data, DoneCallback done)
{
    secrets.insert(key, data);
    QTimer::singleShot(0, this, [done]() { if (done) done(true); });
}

void MemorySecretBackend::load(const QString &key, LoadCallback done)
{
    const bool found = secrets.contains(key);
    const QByteArray data = secrets.value(key);
    QTimer::singleShot(0, this, [done, found, data]() { if (done) done(true, found, data); });
}

void MemorySecretBackend::remove(const QString &key, DoneCallback done)
{
    secrets.remove(key);
    QTimer::singleShot(0, this, [done]() { if (done) done(true); });
}
