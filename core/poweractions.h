// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_POWERACTIONS_H
#define PVEDECK_POWERACTIONS_H

#include <QObject>
#include <QSet>
#include <QStringList>

class SessionManager;

// Start, shutdown, stop, reboot, reset, suspend and resume of guests:
//   POST /nodes/{node}/{type}/{vmid}/status/{action}  ->  UPID
// The task is followed with a TaskWatcher; finished() reports the result
// and the guest's new status is put into the server's guest list. Lives
// as long as the app, so results arrive even after the guest's page was
// closed.
class PowerActions : public QObject
{
    Q_OBJECT
    // Guests with an action in progress, as "<profileId>/<vmid>".
    Q_PROPERTY(QStringList busyGuests READ busyGuests NOTIFY busyGuestsChanged)

public:
    explicit PowerActions(SessionManager *sessions, QObject *parent = nullptr);

    QStringList busyGuests() const;
    // How often tasks are polled, in ms. Default 1000.
    void setPollInterval(int ms);

    // The actions that make sense in the guest's current state, in menu
    // order. qmpStatus is QEMU's finer state ("paused"), empty if unknown.
    Q_INVOKABLE QStringList availableActions(const QString &type, const QString &status,
                                             const QString &qmpStatus, bool isTemplate,
                                             const QString &lock) const;

    // label names the guest in finished(). Returns false if the action
    // can't be sent (not logged in, unknown action, already busy).
    Q_INVOKABLE bool run(const QString &profileId, const QString &node, const QString &type,
                         int vmid, const QString &action, const QString &label);

    static QString guestKey(const QString &profileId, int vmid);

signals:
    void busyGuestsChanged();
    void finished(const QString &profileId, int vmid, const QString &action,
                  const QString &label, bool ok, const QString &message);

private:
    void done(const QString &profileId, const QString &node, const QString &type, int vmid,
              const QString &action, const QString &label, bool ok, const QString &message);
    void updateStatus(const QString &profileId, const QString &node, const QString &type, int vmid);

    SessionManager *m_sessions;
    QSet<QString> m_busy;
    int m_pollInterval = 1000;
};

#endif // PVEDECK_POWERACTIONS_H
