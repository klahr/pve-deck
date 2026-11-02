// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_TASKWATCHER_H
#define PVEDECK_TASKWATCHER_H

#include <QObject>
#include <QPointer>
#include <QTimer>

class PveClient;

// Follows one Proxmox task until it ends:
//   GET /nodes/{node}/tasks/{upid}/status  ->  {status: "running" | "stopped", exitstatus}
// finished() is emitted exactly once. ok is true for exit status "OK"
// and for "WARNINGS: n"; otherwise message is the exit status or error.
class TaskWatcher : public QObject
{
    Q_OBJECT

public:
    TaskWatcher(PveClient *client, const QString &upid, int pollInterval, QObject *parent = nullptr);

    QString upid() const;

    // "UPID:pve1:0000ABCD:..." -> "pve1"
    static QString nodeOf(const QString &upid);

signals:
    void finished(bool ok, const QString &message);

private:
    void poll();
    void finish(bool ok, const QString &message);

    QPointer<PveClient> m_client;
    QString m_upid;
    QTimer m_timer;
    int m_failures = 0;
    bool m_inFlight = false;
    bool m_done = false;
};

#endif // PVEDECK_TASKWATCHER_H
