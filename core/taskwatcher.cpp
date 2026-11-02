// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskwatcher.h"

#include <QJsonObject>

#include "pveclient.h"

namespace {
// Polls that may fail in a row (e.g. a mobile network hiccup) before giving up.
const int MaxFailures = 3;
}

TaskWatcher::TaskWatcher(PveClient *client, const QString &upid, int pollInterval, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_upid(upid)
{
    connect(&m_timer, &QTimer::timeout, this, &TaskWatcher::poll);
    if (client) {
        connect(client, &PveClient::stateChanged, this, [this]() {
            if (!m_client || !m_client->isConnected())
                finish(false, tr("The session ended"));
        });
    }
    if (nodeOf(upid).isEmpty()) {
        // Report asynchronously so the caller can connect first.
        QTimer::singleShot(0, this, [this]() { finish(false, tr("Invalid task ID: %1").arg(m_upid)); });
        return;
    }
    m_timer.start(pollInterval);
}

QString TaskWatcher::upid() const
{
    return m_upid;
}

QString TaskWatcher::nodeOf(const QString &upid)
{
    if (!upid.startsWith(QLatin1String("UPID:")))
        return QString();
    return upid.section(QLatin1Char(':'), 1, 1);
}

void TaskWatcher::poll()
{
    if (m_done || m_inFlight)
        return;
    if (!m_client || !m_client->isConnected()) {
        finish(false, tr("The session ended"));
        return;
    }
    m_inFlight = true;

    QPointer<TaskWatcher> self(this);
    const QString path = QStringLiteral("/nodes/%1/tasks/%2/status").arg(nodeOf(m_upid), m_upid);
    m_client->get(path, [self](const PveClient::Reply &reply) {
        if (!self || self->m_done)
            return;
        self->m_inFlight = false;
        if (!reply.ok) {
            if (++self->m_failures >= MaxFailures)
                self->finish(false, reply.error);
            return;
        }
        self->m_failures = 0;
        const QJsonObject o = reply.data.toObject();
        if (o.value(QStringLiteral("status")).toString() != QLatin1String("stopped"))
            return;
        const QString exit = o.value(QStringLiteral("exitstatus")).toString();
        const bool ok = exit == QLatin1String("OK") || exit.startsWith(QLatin1String("WARNINGS"));
        self->finish(ok, exit);
    });
}

void TaskWatcher::finish(bool ok, const QString &message)
{
    if (m_done)
        return;
    m_done = true;
    m_timer.stop();
    emit finished(ok, message);
}
