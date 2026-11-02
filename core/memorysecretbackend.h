// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_MEMORYSECRETBACKEND_H
#define PVEDECK_MEMORYSECRETBACKEND_H

#include <QHash>

#include "secretbackend.h"

// In-memory secrets; nothing is persisted. Callbacks run on the next
// event loop iteration, like a real asynchronous backend.
class MemorySecretBackend : public SecretBackend
{
    Q_OBJECT

public:
    explicit MemorySecretBackend(QObject *parent = nullptr);

    void store(const QString &key, const QByteArray &data, DoneCallback done) override;
    void load(const QString &key, LoadCallback done) override;
    void remove(const QString &key, DoneCallback done) override;

    QHash<QString, QByteArray> secrets;
};

#endif // PVEDECK_MEMORYSECRETBACKEND_H
