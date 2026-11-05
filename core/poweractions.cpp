// SPDX-License-Identifier: GPL-3.0-or-later
#include "poweractions.h"

#include <QJsonObject>
#include <QPointer>

#include "pveclient.h"
#include "resourcemodel.h"
#include "sessionmanager.h"
#include "taskwatcher.h"

namespace {

bool isSupported(const QString &type, const QString &action)
{
    static const QStringList common = QStringList()
            << QStringLiteral("start") << QStringLiteral("shutdown") << QStringLiteral("stop")
            << QStringLiteral("reboot") << QStringLiteral("suspend") << QStringLiteral("resume");
    if (type == QLatin1String("qemu"))
        return common.contains(action) || action == QLatin1String("reset");
    if (type == QLatin1String("lxc"))
        return common.contains(action);
    return false;
}

} // namespace

PowerActions::PowerActions(SessionManager *sessions, QObject *parent)
    : QObject(parent)
    , m_sessions(sessions)
{
}

QStringList PowerActions::busyGuests() const
{
    return m_busy.toList();
}

void PowerActions::setPollInterval(int ms)
{
    m_pollInterval = ms;
}

QStringList PowerActions::availableActions(const QString &type, const QString &status,
                                           const QString &qmpStatus, bool isTemplate,
                                           const QString &lock) const
{
    QStringList out;
    if (isTemplate || (type != QLatin1String("qemu") && type != QLatin1String("lxc")))
        return out;
    const bool qemu = type == QLatin1String("qemu");

    // A VM hibernated to disk is stopped with lock "suspended"; start resumes it.
    if (!lock.isEmpty() && !(lock == QLatin1String("suspended") && status == QLatin1String("stopped")))
        return out;

    if (status == QLatin1String("stopped")) {
        out << QStringLiteral("start");
    } else if (status == QLatin1String("paused") || qmpStatus == QLatin1String("paused")
               || qmpStatus == QLatin1String("suspended")) {
        out << QStringLiteral("resume") << QStringLiteral("stop");
    } else if (status == QLatin1String("running")) {
        out << QStringLiteral("shutdown") << QStringLiteral("reboot");
        // Suspending containers is experimental in PVE: only offer it for VMs.
        if (qemu)
            out << QStringLiteral("suspend");
        out << QStringLiteral("stop");
        if (qemu)
            out << QStringLiteral("reset");
    }
    return out;
}

bool PowerActions::run(const QString &profileId, const QString &node, const QString &type,
                       int vmid, const QString &action, const QString &label)
{
    PveClient *client = m_sessions->client(profileId);
    if (!client || !client->isConnected() || node.isEmpty() || vmid <= 0 || !isSupported(type, action))
        return false;
    const QString key = guestKey(profileId, vmid);
    if (m_busy.contains(key))
        return false;
    m_busy.insert(key);
    emit busyGuestsChanged();
    // A server removed mid-action never answers: don't leave the guest busy.
    connect(client, &QObject::destroyed, this, [this, key]() {
        if (m_busy.remove(key))
            emit busyGuestsChanged();
    });

    QPointer<PowerActions> self(this);
    const QString path = QStringLiteral("/nodes/%1/%2/%3/status/%4").arg(node, type).arg(vmid).arg(action);
    client->post(path, [self, client, profileId, node, type, vmid, action, label](const PveClient::Reply &reply) {
        if (!self)
            return;
        const QString upid = reply.data.toString();
        if (!reply.ok || upid.isEmpty()) {
            self->done(profileId, node, type, vmid, action, label, false, reply.error);
            return;
        }
        TaskWatcher *watcher = new TaskWatcher(client, upid, self->m_pollInterval, self);
        connect(watcher, &TaskWatcher::finished, self,
                [self, watcher, profileId, node, type, vmid, action, label](bool ok, const QString &message) {
            watcher->deleteLater();
            if (self)
                self->done(profileId, node, type, vmid, action, label, ok, ok ? QString() : message);
        });
    });
    return true;
}

QString PowerActions::guestKey(const QString &profileId, int vmid)
{
    return profileId + QLatin1Char('/') + QString::number(vmid);
}

void PowerActions::done(const QString &profileId, const QString &node, const QString &type,
                        int vmid, const QString &action, const QString &label, bool ok,
                        const QString &message)
{
    if (m_busy.remove(guestKey(profileId, vmid)))
        emit busyGuestsChanged();
    emit finished(profileId, vmid, action, label, ok, message);
    updateStatus(profileId, node, type, vmid);
}

void PowerActions::updateStatus(const QString &profileId, const QString &node, const QString &type,
                                int vmid)
{
    // The guest list comes from /cluster/resources, which can show the old
    // state for several more seconds: ask the guest itself.
    PveClient *client = m_sessions->client(profileId);
    if (!client || !client->isConnected())
        return;
    QPointer<PowerActions> self(this);
    const QString path = QStringLiteral("/nodes/%1/%2/%3/status/current").arg(node, type).arg(vmid);
    client->get(path, [self, profileId, vmid](const PveClient::Reply &reply) {
        if (!self || !reply.ok)
            return;
        if (ResourceModel *resources = self->m_sessions->resources(profileId))
            resources->overrideStatus(vmid, reply.data.toObject().value(QStringLiteral("status")).toString());
    });
}
