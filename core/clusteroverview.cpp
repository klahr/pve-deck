// SPDX-License-Identifier: GPL-3.0-or-later
#include "clusteroverview.h"

#include <QHash>
#include <QJsonObject>

#include <algorithm>

namespace {

qint64 toInt64(const QJsonValue &v)
{
    return static_cast<qint64>(v.toDouble());
}

bool byName(const ClusterOverview::Node &a, const ClusterOverview::Node &b)
{
    return QString::compare(a.name, b.name) < 0;
}

} // namespace

bool ClusterOverview::Node::operator==(const Node &o) const
{
    return name == o.name && online == o.online && local == o.local && address == o.address
            && cpu == o.cpu && maxcpu == o.maxcpu && mem == o.mem && maxmem == o.maxmem
            && disk == o.disk && maxdisk == o.maxdisk && uptime == o.uptime
            && guests == o.guests && runningGuests == o.runningGuests;
}

ClusterOverview::ClusterOverview(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ClusterOverview::refresh);
}

int ClusterOverview::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_nodes.size();
}

QVariant ClusterOverview::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_nodes.size())
        return QVariant();
    const Node &n = m_nodes.at(index.row());
    switch (role) {
    case NameRole: return n.name;
    case OnlineRole: return n.online;
    case LocalRole: return n.local;
    case AddressRole: return n.address;
    case CpuRole: return n.cpu;
    case MaxCpuRole: return n.maxcpu;
    case MemRole: return static_cast<double>(n.mem);
    case MaxMemRole: return static_cast<double>(n.maxmem);
    case DiskRole: return static_cast<double>(n.disk);
    case MaxDiskRole: return static_cast<double>(n.maxdisk);
    case UptimeRole: return static_cast<double>(n.uptime);
    case GuestsRole: return n.guests;
    case RunningGuestsRole: return n.runningGuests;
    }
    return QVariant();
}

QHash<int, QByteArray> ClusterOverview::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(NameRole, "name");
    roles.insert(OnlineRole, "online");
    roles.insert(LocalRole, "local");
    roles.insert(AddressRole, "address");
    roles.insert(CpuRole, "cpu");
    roles.insert(MaxCpuRole, "maxcpu");
    roles.insert(MemRole, "mem");
    roles.insert(MaxMemRole, "maxmem");
    roles.insert(DiskRole, "disk");
    roles.insert(MaxDiskRole, "maxdisk");
    roles.insert(UptimeRole, "uptime");
    roles.insert(GuestsRole, "guests");
    roles.insert(RunningGuestsRole, "runningGuests");
    return roles;
}

PveClient *ClusterOverview::client() const
{
    return m_client;
}

void ClusterOverview::setClient(PveClient *client)
{
    if (client == m_client)
        return;
    if (m_client)
        disconnect(m_client, nullptr, this, nullptr);
    m_client = client;
    if (client) {
        connect(client, &PveClient::stateChanged, this, [this]() {
            if (m_client && m_client->isConnected())
                refresh();
            else
                clear();
            updateTimer();
        });
    }
    clear();
    refresh();
    updateTimer();
    emit clientChanged();
}

int ClusterOverview::pollInterval() const
{
    return m_pollInterval;
}

void ClusterOverview::setPollInterval(int ms)
{
    if (ms == m_pollInterval)
        return;
    m_pollInterval = ms;
    updateTimer();
    emit pollIntervalChanged();
}

bool ClusterOverview::isCluster() const
{
    return m_isCluster;
}

QString ClusterOverview::clusterName() const
{
    return m_clusterName;
}

bool ClusterOverview::isQuorate() const
{
    return m_quorate;
}

bool ClusterOverview::isQuorumKnown() const
{
    return m_quorumKnown;
}

int ClusterOverview::count() const
{
    return m_nodes.size();
}

int ClusterOverview::onlineCount() const
{
    int n = 0;
    foreach (const Node &node, m_nodes)
        n += node.online ? 1 : 0;
    return n;
}

bool ClusterOverview::isLoading() const
{
    return m_loading;
}

bool ClusterOverview::isLoaded() const
{
    return m_loaded;
}

QString ClusterOverview::errorString() const
{
    return m_error;
}

void ClusterOverview::refresh()
{
    if (!m_client || !m_client->isConnected() || m_loading)
        return;
    setLoading(true);
    const int generation = ++m_generation;
    m_pending = 2;
    m_statusOk = false;
    m_resourcesOk = false;

    QPointer<ClusterOverview> self(this);
    m_client->get(QStringLiteral("/cluster/status"), [self, generation](const PveClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        self->m_statusOk = reply.ok;
        self->m_status = reply.data.toArray();
        self->finishRefresh();
    });
    m_client->get(QStringLiteral("/cluster/resources"), [self, generation](const PveClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        self->m_resourcesOk = reply.ok;
        self->m_resources = reply.data.toArray();
        self->m_resourcesError = reply.error;
        self->finishRefresh();
    });
}

void ClusterOverview::update(const QJsonArray &status, bool statusKnown, const QJsonArray &resources)
{
    // Nodes and their guests from /cluster/resources.
    QHash<QString, Node> nodes;
    for (const QJsonValue &v : resources) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        const QString name = o.value(QStringLiteral("node")).toString();
        if (name.isEmpty())
            continue;
        if (type == QLatin1String("node")) {
            Node &n = nodes[name];
            n.name = name;
            n.online = o.value(QStringLiteral("status")).toString() == QLatin1String("online");
            n.cpu = o.value(QStringLiteral("cpu")).toDouble();
            n.maxcpu = o.value(QStringLiteral("maxcpu")).toInt();
            n.mem = toInt64(o.value(QStringLiteral("mem")));
            n.maxmem = toInt64(o.value(QStringLiteral("maxmem")));
            n.disk = toInt64(o.value(QStringLiteral("disk")));
            n.maxdisk = toInt64(o.value(QStringLiteral("maxdisk")));
            n.uptime = toInt64(o.value(QStringLiteral("uptime")));
        } else if ((type == QLatin1String("qemu") || type == QLatin1String("lxc"))
                   && o.value(QStringLiteral("template")).toInt() != 1) {
            Node &n = nodes[name];
            n.name = name;
            ++n.guests;
            if (o.value(QStringLiteral("status")).toString() == QLatin1String("running"))
                ++n.runningGuests;
        }
    }

    // Cluster name, quorum, and the nodes' own view of who is online.
    bool isCluster = false;
    QString clusterName;
    bool quorate = false;
    bool quorumKnown = false;
    for (const QJsonValue &v : status) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("cluster")) {
            isCluster = true;
            clusterName = o.value(QStringLiteral("name")).toString();
            quorate = o.value(QStringLiteral("quorate")).toInt() == 1;
            quorumKnown = true;
        } else if (type == QLatin1String("node")) {
            const QString name = o.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            Node &n = nodes[name];
            n.name = name;
            n.online = o.value(QStringLiteral("online")).toInt() == 1;
            n.local = o.value(QStringLiteral("local")).toInt() == 1;
            n.address = o.value(QStringLiteral("ip")).toString();
        }
    }
    if (!statusKnown) {
        // Without /cluster/status: a cluster has more than one node.
        isCluster = nodes.size() > 1;
    }

    QList<Node> next = nodes.values();
    std::sort(next.begin(), next.end(), byName);

    bool sameNames = next.size() == m_nodes.size();
    for (int i = 0; sameNames && i < next.size(); ++i)
        sameNames = next.at(i).name == m_nodes.at(i).name;
    if (sameNames) {
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i) != m_nodes.at(i)) {
                m_nodes[i] = next.at(i);
                emit dataChanged(index(i), index(i));
            }
        }
    } else {
        // Nodes are rarely added or removed: a reset is fine.
        beginResetModel();
        m_nodes = next;
        endResetModel();
    }

    m_isCluster = isCluster;
    m_clusterName = clusterName;
    m_quorate = quorate;
    m_quorumKnown = quorumKnown;
    m_loaded = true;
    emit summaryChanged();
}

void ClusterOverview::finishRefresh()
{
    if (--m_pending > 0)
        return;
    setLoading(false);
    if (!m_resourcesOk) {
        setErrorString(m_resourcesError);
        return;
    }
    setErrorString(QString());
    update(m_statusOk ? m_status : QJsonArray(), m_statusOk, m_resources);
}

void ClusterOverview::clear()
{
    ++m_generation;
    setLoading(false);
    setErrorString(QString());
    if (!m_nodes.isEmpty()) {
        beginResetModel();
        m_nodes.clear();
        endResetModel();
    }
    m_isCluster = false;
    m_clusterName.clear();
    m_quorate = false;
    m_quorumKnown = false;
    m_loaded = false;
    emit summaryChanged();
}

void ClusterOverview::updateTimer()
{
    if (m_pollInterval > 0 && m_client && m_client->isConnected()) {
        if (m_timer.interval() != m_pollInterval || !m_timer.isActive())
            m_timer.start(m_pollInterval);
    } else {
        m_timer.stop();
    }
}

void ClusterOverview::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void ClusterOverview::setErrorString(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorStringChanged();
}
