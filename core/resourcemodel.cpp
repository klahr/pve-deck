// SPDX-License-Identifier: GPL-3.0-or-later
#include "resourcemodel.h"

#include <QRegExp>

#include <algorithm>

#include "pveclient.h"

namespace {

bool lessThan(const ResourceModel::Guest &a, const ResourceModel::Guest &b)
{
    const int byNode = QString::compare(a.node, b.node);
    if (byNode != 0)
        return byNode < 0;
    return a.vmid < b.vmid;
}

qint64 toInt64(const QJsonValue &v)
{
    return static_cast<qint64>(v.toDouble());
}

} // namespace

bool ResourceModel::Guest::operator==(const Guest &o) const
{
    return vmid == o.vmid && name == o.name && node == o.node && type == o.type
            && status == o.status && cpu == o.cpu && maxcpu == o.maxcpu
            && mem == o.mem && maxmem == o.maxmem && disk == o.disk
            && maxdisk == o.maxdisk && uptime == o.uptime && isTemplate == o.isTemplate
            && tags == o.tags && lock == o.lock;
}

ResourceModel::ResourceModel(PveClient *client, QObject *parent)
    : QAbstractListModel(parent)
    , m_client(client)
{
    connect(&m_timer, &QTimer::timeout, this, &ResourceModel::refresh);
    if (client) {
        connect(client, &PveClient::stateChanged, this, &ResourceModel::onClientStateChanged);
        if (client->isConnected())
            refresh();
    }
}

int ResourceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_guests.size();
}

QVariant ResourceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_guests.size())
        return QVariant();
    const Guest &g = m_guests.at(index.row());
    switch (role) {
    case VmidRole: return g.vmid;
    case NameRole: return g.name;
    case NodeRole: return g.node;
    case TypeRole: return g.type;
    case StatusRole: return g.status;
    case CpuRole: return g.cpu;
    case MaxCpuRole: return g.maxcpu;
    case MemRole: return static_cast<double>(g.mem);
    case MaxMemRole: return static_cast<double>(g.maxmem);
    case DiskRole: return static_cast<double>(g.disk);
    case MaxDiskRole: return static_cast<double>(g.maxdisk);
    case UptimeRole: return static_cast<double>(g.uptime);
    case TemplateRole: return g.isTemplate;
    case TagsRole: return g.tags;
    case LockRole: return g.lock;
    }
    return QVariant();
}

QHash<int, QByteArray> ResourceModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(VmidRole, "vmid");
    roles.insert(NameRole, "name");
    roles.insert(NodeRole, "node");
    roles.insert(TypeRole, "type");
    roles.insert(StatusRole, "status");
    roles.insert(CpuRole, "cpu");
    roles.insert(MaxCpuRole, "maxcpu");
    roles.insert(MemRole, "mem");
    roles.insert(MaxMemRole, "maxmem");
    roles.insert(DiskRole, "disk");
    roles.insert(MaxDiskRole, "maxdisk");
    roles.insert(UptimeRole, "uptime");
    roles.insert(TemplateRole, "template");
    roles.insert(TagsRole, "tags");
    roles.insert(LockRole, "lock");
    return roles;
}

int ResourceModel::count() const
{
    return m_guests.size();
}

int ResourceModel::runningCount() const
{
    return m_running;
}

int ResourceModel::stoppedCount() const
{
    return m_stopped;
}

bool ResourceModel::isLoading() const
{
    return m_loading;
}

bool ResourceModel::isLoaded() const
{
    return m_loaded;
}

QString ResourceModel::errorString() const
{
    return m_error;
}

int ResourceModel::pollInterval() const
{
    return m_pollInterval;
}

void ResourceModel::setPollInterval(int ms)
{
    if (ms == m_pollInterval)
        return;
    m_pollInterval = ms;
    updateTimer();
    emit pollIntervalChanged();
}

void ResourceModel::refresh()
{
    if (!m_client || !m_client->isConnected() || m_loading)
        return;
    setLoading(true);

    const int generation = m_generation;
    QPointer<ResourceModel> self(this);
    PveClient::FormFields query;
    query.append(qMakePair(QStringLiteral("type"), QStringLiteral("vm")));
    m_client->get(QStringLiteral("/cluster/resources"), [self, generation](const PveClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        self->setLoading(false);
        if (!reply.ok) {
            self->setErrorString(reply.error);
            return;
        }
        self->setErrorString(QString());
        self->update(reply.data.toArray());
    }, query);
}

QVariantMap ResourceModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_guests.size())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (QHash<int, QByteArray>::const_iterator it = roles.constBegin(); it != roles.constEnd(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

ResourceModel::Guest ResourceModel::parseGuest(const QJsonObject &o)
{
    Guest g;
    g.vmid = o.value(QStringLiteral("vmid")).toInt();
    g.name = o.value(QStringLiteral("name")).toString();
    g.node = o.value(QStringLiteral("node")).toString();
    g.type = o.value(QStringLiteral("type")).toString();
    g.status = o.value(QStringLiteral("status")).toString();
    g.cpu = o.value(QStringLiteral("cpu")).toDouble();
    g.maxcpu = o.value(QStringLiteral("maxcpu")).toInt();
    g.mem = toInt64(o.value(QStringLiteral("mem")));
    g.maxmem = toInt64(o.value(QStringLiteral("maxmem")));
    g.disk = toInt64(o.value(QStringLiteral("disk")));
    g.maxdisk = toInt64(o.value(QStringLiteral("maxdisk")));
    g.uptime = toInt64(o.value(QStringLiteral("uptime")));
    g.isTemplate = o.value(QStringLiteral("template")).toInt() == 1;
    // PVE stores tags separated by ';', but also accepts ',' and spaces.
    g.tags = o.value(QStringLiteral("tags")).toString()
            .split(QRegExp(QStringLiteral("[;,\\s]+")), QString::SkipEmptyParts);
    g.lock = o.value(QStringLiteral("lock")).toString();
    return g;
}

void ResourceModel::update(const QJsonArray &resources)
{
    QList<Guest> next;
    for (const QJsonValue &v : resources) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("qemu") && type != QLatin1String("lxc"))
            continue;
        next.append(parseGuest(o));
    }
    std::sort(next.begin(), next.end(), lessThan);

    const int oldCount = m_guests.size();

    // Both lists are sorted by (node, vmid): walk them together.
    int i = 0;
    int j = 0;
    while (i < m_guests.size() || j < next.size()) {
        if (j >= next.size() || (i < m_guests.size() && lessThan(m_guests.at(i), next.at(j)))) {
            beginRemoveRows(QModelIndex(), i, i);
            m_guests.removeAt(i);
            endRemoveRows();
        } else if (i >= m_guests.size() || lessThan(next.at(j), m_guests.at(i))) {
            beginInsertRows(QModelIndex(), i, i);
            m_guests.insert(i, next.at(j));
            endInsertRows();
            ++i;
            ++j;
        } else {
            if (m_guests.at(i) != next.at(j)) {
                m_guests[i] = next.at(j);
                emit dataChanged(index(i), index(i));
            }
            ++i;
            ++j;
        }
    }

    int running = 0;
    int stopped = 0;
    foreach (const Guest &g, m_guests) {
        if (g.isTemplate)
            continue;
        if (g.status == QLatin1String("running"))
            ++running;
        else if (g.status == QLatin1String("stopped"))
            ++stopped;
    }
    const bool countsDiffer = running != m_running || stopped != m_stopped;
    m_running = running;
    m_stopped = stopped;

    if (m_guests.size() != oldCount)
        emit countChanged();
    if (countsDiffer)
        emit countsChanged();
    if (!m_loaded) {
        m_loaded = true;
        emit loadedChanged();
    }
}

void ResourceModel::clear()
{
    ++m_generation;
    setLoading(false);
    setErrorString(QString());

    const bool hadRows = !m_guests.isEmpty();
    const bool hadCounts = m_running != 0 || m_stopped != 0;
    if (hadRows) {
        beginResetModel();
        m_guests.clear();
        endResetModel();
    }
    m_running = 0;
    m_stopped = 0;
    if (hadRows)
        emit countChanged();
    if (hadCounts)
        emit countsChanged();
    if (m_loaded) {
        m_loaded = false;
        emit loadedChanged();
    }
}

void ResourceModel::onClientStateChanged()
{
    if (m_client && m_client->isConnected()) {
        refresh();
    } else {
        clear();
    }
    updateTimer();
}

void ResourceModel::updateTimer()
{
    if (m_pollInterval > 0 && m_client && m_client->isConnected()) {
        if (m_timer.interval() != m_pollInterval || !m_timer.isActive())
            m_timer.start(m_pollInterval);
    } else {
        m_timer.stop();
    }
}

void ResourceModel::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void ResourceModel::setErrorString(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorStringChanged();
}
