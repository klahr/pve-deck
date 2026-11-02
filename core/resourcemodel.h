// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_RESOURCEMODEL_H
#define PVEDECK_RESOURCEMODEL_H

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class PveClient;

// The guests (QEMU VMs and LXC containers) of one server, from
// GET /cluster/resources?type=vm. Sorted by node, then vmid.
// Refreshes are merged row by row, so views don't jump or lose their
// position. Polls every pollInterval ms while the client is connected
// and empties itself when the client disconnects.
class ResourceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY countsChanged)
    Q_PROPERTY(int stoppedCount READ stoppedCount NOTIFY countsChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval NOTIFY pollIntervalChanged)

public:
    enum Roles {
        VmidRole = Qt::UserRole + 1,
        NameRole,
        NodeRole,
        TypeRole,
        StatusRole,
        CpuRole,
        MaxCpuRole,
        MemRole,
        MaxMemRole,
        DiskRole,
        MaxDiskRole,
        UptimeRole,
        TemplateRole,
        TagsRole,
        LockRole
    };

    struct Guest {
        int vmid = 0;
        QString name;
        QString node;
        QString type;       // "qemu" or "lxc"
        QString status;     // "running", "stopped", ...
        double cpu = 0;     // fraction of maxcpu
        int maxcpu = 0;
        qint64 mem = 0;
        qint64 maxmem = 0;
        qint64 disk = 0;
        qint64 maxdisk = 0;
        qint64 uptime = 0;
        bool isTemplate = false;
        QStringList tags;
        QString lock;

        bool operator==(const Guest &o) const;
        bool operator!=(const Guest &o) const { return !(*this == o); }
    };

    explicit ResourceModel(PveClient *client, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int runningCount() const;
    int stoppedCount() const;
    bool isLoading() const;
    bool isLoaded() const;
    QString errorString() const;
    int pollInterval() const;
    void setPollInterval(int ms);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;

    // Merge a /cluster/resources result into the model. Public for tests.
    void update(const QJsonArray &resources);
    void clear();

    static Guest parseGuest(const QJsonObject &o);

signals:
    void countChanged();
    void countsChanged();
    void loadingChanged();
    void loadedChanged();
    void errorStringChanged();
    void pollIntervalChanged();

private:
    void onClientStateChanged();
    void updateTimer();
    void setLoading(bool loading);
    void setErrorString(const QString &error);

    QPointer<PveClient> m_client;
    QList<Guest> m_guests;
    QTimer m_timer;
    int m_pollInterval = 0;
    int m_generation = 0;
    int m_running = 0;
    int m_stopped = 0;
    bool m_loading = false;
    bool m_loaded = false;
    QString m_error;
};

#endif // PVEDECK_RESOURCEMODEL_H
