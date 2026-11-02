// SPDX-License-Identifier: GPL-3.0-or-later
#include "guestdetails.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {

bool isUsableAddress(const QString &address)
{
    const QString lower = address.toLower();
    return !address.isEmpty()
            && !address.startsWith(QLatin1String("127."))
            && lower != QLatin1String("::1")
            && !lower.startsWith(QLatin1String("fe80:"));
}

// "10.0.0.5/24" -> "10.0.0.5"
QString stripPrefix(const QString &cidr)
{
    const int slash = cidr.indexOf(QLatin1Char('/'));
    return slash < 0 ? cidr : cidr.left(slash);
}

} // namespace

GuestDetails::GuestDetails(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, [this]() { fetchStatus(false); });
}

PveClient *GuestDetails::client() const
{
    return m_client;
}

void GuestDetails::setClient(PveClient *client)
{
    if (client == m_client)
        return;
    if (m_client)
        disconnect(m_client, nullptr, this, nullptr);
    m_client = client;
    if (client) {
        connect(client, &PveClient::stateChanged, this, [this]() {
            updateTimer();
            if (m_client && m_client->isConnected())
                refresh();
        });
    }
    scheduleReset();
    emit guestChanged();
}

QString GuestDetails::node() const
{
    return m_node;
}

void GuestDetails::setNode(const QString &node)
{
    if (node == m_node)
        return;
    m_node = node;
    scheduleReset();
    emit guestChanged();
}

QString GuestDetails::type() const
{
    return m_type;
}

void GuestDetails::setType(const QString &type)
{
    if (type == m_type)
        return;
    m_type = type;
    scheduleReset();
    emit guestChanged();
}

int GuestDetails::vmid() const
{
    return m_vmid;
}

void GuestDetails::setVmid(int vmid)
{
    if (vmid == m_vmid)
        return;
    m_vmid = vmid;
    scheduleReset();
    emit guestChanged();
}

int GuestDetails::pollInterval() const
{
    return m_pollInterval;
}

void GuestDetails::setPollInterval(int ms)
{
    if (ms == m_pollInterval)
        return;
    m_pollInterval = ms;
    updateTimer();
    emit pollIntervalChanged();
}

QVariantMap GuestDetails::status() const
{
    return m_status;
}

QStringList GuestDetails::addresses() const
{
    return m_addresses;
}

bool GuestDetails::isLoading() const
{
    return m_loading;
}

bool GuestDetails::isLoaded() const
{
    return m_loaded;
}

QString GuestDetails::errorString() const
{
    return m_error;
}

void GuestDetails::refresh()
{
    fetchStatus(true);
}

QStringList GuestDetails::parseAgentAddresses(const QJsonValue &data)
{
    // {"result": [{"name": "eth0", "ip-addresses": [{"ip-address": "...", ...}]}]}
    QStringList out;
    for (const QJsonValue &iface : data.toObject().value(QStringLiteral("result")).toArray()) {
        const QJsonObject o = iface.toObject();
        if (o.value(QStringLiteral("name")).toString() == QLatin1String("lo"))
            continue;
        for (const QJsonValue &ip : o.value(QStringLiteral("ip-addresses")).toArray()) {
            const QString address = ip.toObject().value(QStringLiteral("ip-address")).toString();
            if (isUsableAddress(address) && !out.contains(address))
                out.append(address);
        }
    }
    return out;
}

QStringList GuestDetails::parseLxcAddresses(const QJsonValue &data)
{
    // [{"name": "eth0", "inet": "10.0.0.5/24", "inet6": "fd00::5/64"}, ...]
    QStringList out;
    for (const QJsonValue &iface : data.toArray()) {
        const QJsonObject o = iface.toObject();
        if (o.value(QStringLiteral("name")).toString() == QLatin1String("lo"))
            continue;
        const QString keys[] = { QStringLiteral("inet"), QStringLiteral("inet6") };
        for (const QString &key : keys) {
            const QString address = stripPrefix(o.value(key).toString());
            if (isUsableAddress(address) && !out.contains(address))
                out.append(address);
        }
    }
    return out;
}

QString GuestDetails::basePath() const
{
    return QStringLiteral("/nodes/%1/%2/%3").arg(m_node, m_type).arg(m_vmid);
}

bool GuestDetails::isValid() const
{
    return m_client && m_client->isConnected() && !m_node.isEmpty() && m_vmid > 0
            && (m_type == QLatin1String("qemu") || m_type == QLatin1String("lxc"));
}

void GuestDetails::fetchStatus(bool withAddresses)
{
    if (!isValid() || (m_loading && !withAddresses))
        return;
    setLoading(true);

    const int generation = m_generation;
    QPointer<GuestDetails> self(this);
    m_client->get(basePath() + QStringLiteral("/status/current"),
                  [self, generation, withAddresses](const PveClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        self->setLoading(false);
        if (!reply.ok) {
            self->setErrorString(reply.error);
            return;
        }
        self->setErrorString(QString());

        const bool wasRunning = self->m_status.value(QStringLiteral("status")).toString()
                == QLatin1String("running");
        self->m_status = reply.data.toObject().toVariantMap();
        self->m_loaded = true;
        emit self->statusChanged();

        const bool running = self->m_status.value(QStringLiteral("status")).toString()
                == QLatin1String("running");
        if (!running)
            self->setAddresses(QStringList());
        else if (withAddresses || !wasRunning)
            self->fetchAddresses();
    });
}

void GuestDetails::fetchAddresses()
{
    if (!isValid())
        return;
    const bool qemu = m_type == QLatin1String("qemu");
    // Without an agent configured the call can only fail.
    if (qemu && m_status.value(QStringLiteral("agent")).toInt() != 1) {
        setAddresses(QStringList());
        return;
    }

    const int generation = m_generation;
    QPointer<GuestDetails> self(this);
    const QString path = basePath()
            + (qemu ? QStringLiteral("/agent/network-get-interfaces") : QStringLiteral("/interfaces"));
    m_client->get(path, [self, generation, qemu](const PveClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        // A guest agent that isn't running is normal: just show no addresses.
        if (!reply.ok) {
            self->setAddresses(QStringList());
            return;
        }
        self->setAddresses(qemu ? parseAgentAddresses(reply.data) : parseLxcAddresses(reply.data));
    });
}

void GuestDetails::scheduleReset()
{
    // QML sets client, node, type and vmid one at a time: reset once.
    if (m_resetPending)
        return;
    m_resetPending = true;
    QTimer::singleShot(0, this, [this]() { guestReset(); });
}

void GuestDetails::guestReset()
{
    m_resetPending = false;
    ++m_generation;
    setLoading(false);
    setErrorString(QString());
    setAddresses(QStringList());
    if (m_loaded || !m_status.isEmpty()) {
        m_status.clear();
        m_loaded = false;
        emit statusChanged();
    }
    updateTimer();
    refresh();
}

void GuestDetails::updateTimer()
{
    if (m_pollInterval > 0 && isValid()) {
        if (m_timer.interval() != m_pollInterval || !m_timer.isActive())
            m_timer.start(m_pollInterval);
    } else {
        m_timer.stop();
    }
}

void GuestDetails::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void GuestDetails::setErrorString(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorStringChanged();
}

void GuestDetails::setAddresses(const QStringList &addresses)
{
    if (addresses == m_addresses)
        return;
    m_addresses = addresses;
    emit addressesChanged();
}
