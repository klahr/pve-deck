// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_CLUSTEROVERVIEW_H
#define PVEDECK_CLUSTEROVERVIEW_H

#include <QAbstractListModel>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>

#include "pveclient.h"

// The nodes of a server's cluster, sorted by name:
//   GET /cluster/status     cluster name, quorum, each node's online state and IP
//   GET /cluster/resources  each node's CPU, memory, disk and uptime, and its guests
// /cluster/status needs Sys.Audit; without it the nodes are still listed,
// only the cluster name and quorum are unknown. A server that is not in a
// cluster has no cluster entry: isCluster is false.
class ClusterOverview : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(PveClient *client READ client WRITE setClient NOTIFY clientChanged)
    Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval NOTIFY pollIntervalChanged)
    Q_PROPERTY(bool isCluster READ isCluster NOTIFY summaryChanged)
    Q_PROPERTY(QString clusterName READ clusterName NOTIFY summaryChanged)
    // Only meaningful when quorumKnown is true.
    Q_PROPERTY(bool quorate READ isQuorate NOTIFY summaryChanged)
    Q_PROPERTY(bool quorumKnown READ isQuorumKnown NOTIFY summaryChanged)
    Q_PROPERTY(int count READ count NOTIFY summaryChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY summaryChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY summaryChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        OnlineRole,
        LocalRole,        // the node this API connection is served by
        AddressRole,
        CpuRole,
        MaxCpuRole,
        MemRole,
        MaxMemRole,
        DiskRole,
        MaxDiskRole,
        UptimeRole,
        GuestsRole,       // VMs and containers on the node, without templates
        RunningGuestsRole
    };

    struct Node {
        QString name;
        bool online = false;
        bool local = false;
        QString address;
        double cpu = 0;
        int maxcpu = 0;
        qint64 mem = 0;
        qint64 maxmem = 0;
        qint64 disk = 0;
        qint64 maxdisk = 0;
        qint64 uptime = 0;
        int guests = 0;
        int runningGuests = 0;

        bool operator==(const Node &o) const;
        bool operator!=(const Node &o) const { return !(*this == o); }
    };

    explicit ClusterOverview(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    PveClient *client() const;
    void setClient(PveClient *client);
    int pollInterval() const;
    void setPollInterval(int ms);
    bool isCluster() const;
    QString clusterName() const;
    bool isQuorate() const;
    bool isQuorumKnown() const;
    int count() const;
    int onlineCount() const;
    bool isLoading() const;
    bool isLoaded() const;
    QString errorString() const;

    Q_INVOKABLE void refresh();

    // Combine the two API results. status may be empty when /cluster/status
    // was not allowed. Public for tests.
    void update(const QJsonArray &status, bool statusKnown, const QJsonArray &resources);

signals:
    void clientChanged();
    void pollIntervalChanged();
    void summaryChanged();
    void loadingChanged();
    void errorStringChanged();

private:
    void finishRefresh();
    void clear();
    void updateTimer();
    void setLoading(bool loading);
    void setErrorString(const QString &error);

    QPointer<PveClient> m_client;
    QList<Node> m_nodes;
    QTimer m_timer;
    int m_pollInterval = 0;
    int m_generation = 0;
    int m_pending = 0;
    QJsonArray m_status;
    bool m_statusOk = false;
    QJsonArray m_resources;
    bool m_resourcesOk = false;
    QString m_resourcesError;
    bool m_isCluster = false;
    QString m_clusterName;
    bool m_quorate = false;
    bool m_quorumKnown = false;
    bool m_loading = false;
    bool m_loaded = false;
    QString m_error;
};

#endif // PVEDECK_CLUSTEROVERVIEW_H
