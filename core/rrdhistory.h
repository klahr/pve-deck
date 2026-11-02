// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_RRDHISTORY_H
#define PVEDECK_RRDHISTORY_H

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "pveclient.h"

// Usage history of a guest or node, as kept by Proxmox (about 70 points
// per timeframe):
//   GET /nodes/{node}/{type}/{vmid}/rrddata?timeframe=..&cf=AVERAGE
//   GET /nodes/{node}/rrddata?timeframe=..&cf=AVERAGE      (type empty)
// Series are named as in the API: cpu, maxcpu, mem, maxmem, netin, netout,
// diskread, diskwrite, ... A missing sample is NaN, drawn as a gap.
class RrdHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PveClient *client READ client WRITE setClient NOTIFY targetChanged)
    Q_PROPERTY(QString node READ node WRITE setNode NOTIFY targetChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY targetChanged)
    Q_PROPERTY(int vmid READ vmid WRITE setVmid NOTIFY targetChanged)
    // "hour", "day", "week", "month" or "year".
    Q_PROPERTY(QString timeframe READ timeframe WRITE setTimeframe NOTIFY targetChanged)
    Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval NOTIFY pollIntervalChanged)
    Q_PROPERTY(int count READ count NOTIFY historyChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY historyChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit RrdHistory(QObject *parent = nullptr);

    PveClient *client() const;
    void setClient(PveClient *client);
    QString node() const;
    void setNode(const QString &node);
    QString type() const;
    void setType(const QString &type);
    int vmid() const;
    void setVmid(int vmid);
    QString timeframe() const;
    void setTimeframe(const QString &timeframe);
    int pollInterval() const;
    void setPollInterval(int ms);

    int count() const;
    bool isLoaded() const;
    bool isLoading() const;
    QString errorString() const;

    // Sample times in seconds since the epoch, oldest first.
    QVector<qint64> times() const;
    // Same length as times(); NaN where the sample is missing.
    QVector<double> values(const QString &key) const;

    Q_INVOKABLE qint64 timeAt(int index) const;
    Q_INVOKABLE double valueAt(const QString &key, int index) const;
    // The newest sample that has a value, NaN if none.
    Q_INVOKABLE double latest(const QString &key) const;

    Q_INVOKABLE void refresh();

    // Load an rrddata result. Public for tests.
    void update(const QJsonArray &data);

signals:
    void targetChanged();
    void pollIntervalChanged();
    void historyChanged();
    void loadingChanged();
    void errorStringChanged();

private:
    QString path() const;
    bool isValid() const;
    void scheduleReset();
    void reset();
    void updateTimer();
    void setLoading(bool loading);
    void setErrorString(const QString &error);

    QPointer<PveClient> m_client;
    QString m_node;
    QString m_type;
    int m_vmid = 0;
    QString m_timeframe = QStringLiteral("hour");
    int m_pollInterval = 0;
    int m_generation = 0;
    bool m_resetPending = false;
    QTimer m_timer;
    QVector<qint64> m_times;
    QHash<QString, QVector<double> > m_series;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_error;
};

#endif // PVEDECK_RRDHISTORY_H
