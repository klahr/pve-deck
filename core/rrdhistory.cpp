// SPDX-License-Identifier: GPL-3.0-or-later
#include "rrdhistory.h"

#include <QJsonObject>

#include <cmath>
#include <limits>

namespace {
const double Missing = std::numeric_limits<double>::quiet_NaN();
}

RrdHistory::RrdHistory(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &RrdHistory::refresh);
}

PveClient *RrdHistory::client() const
{
    return m_client;
}

void RrdHistory::setClient(PveClient *client)
{
    if (client == m_client)
        return;
    if (m_client)
        disconnect(m_client, nullptr, this, nullptr);
    m_client = client;
    if (client)
        connect(client, &PveClient::stateChanged, this, &RrdHistory::scheduleReset);
    scheduleReset();
    emit targetChanged();
}

QString RrdHistory::node() const
{
    return m_node;
}

void RrdHistory::setNode(const QString &node)
{
    if (node == m_node)
        return;
    m_node = node;
    scheduleReset();
    emit targetChanged();
}

QString RrdHistory::type() const
{
    return m_type;
}

void RrdHistory::setType(const QString &type)
{
    if (type == m_type)
        return;
    m_type = type;
    scheduleReset();
    emit targetChanged();
}

int RrdHistory::vmid() const
{
    return m_vmid;
}

void RrdHistory::setVmid(int vmid)
{
    if (vmid == m_vmid)
        return;
    m_vmid = vmid;
    scheduleReset();
    emit targetChanged();
}

QString RrdHistory::timeframe() const
{
    return m_timeframe;
}

void RrdHistory::setTimeframe(const QString &timeframe)
{
    if (timeframe == m_timeframe)
        return;
    m_timeframe = timeframe;
    scheduleReset();
    emit targetChanged();
}

int RrdHistory::pollInterval() const
{
    return m_pollInterval;
}

void RrdHistory::setPollInterval(int ms)
{
    if (ms == m_pollInterval)
        return;
    m_pollInterval = ms;
    updateTimer();
    emit pollIntervalChanged();
}

int RrdHistory::count() const
{
    return m_times.size();
}

bool RrdHistory::isLoaded() const
{
    return m_loaded;
}

bool RrdHistory::isLoading() const
{
    return m_loading;
}

QString RrdHistory::errorString() const
{
    return m_error;
}

QVector<qint64> RrdHistory::times() const
{
    return m_times;
}

QVector<double> RrdHistory::values(const QString &key) const
{
    if (!m_series.contains(key))
        return QVector<double>(m_times.size(), Missing);
    return m_series.value(key);
}

qint64 RrdHistory::timeAt(int index) const
{
    return index >= 0 && index < m_times.size() ? m_times.at(index) : 0;
}

double RrdHistory::valueAt(const QString &key, int index) const
{
    if (index < 0 || index >= m_times.size() || !m_series.contains(key))
        return Missing;
    return m_series.value(key).at(index);
}

double RrdHistory::latest(const QString &key) const
{
    const QVector<double> series = m_series.value(key);
    for (int i = series.size() - 1; i >= 0; --i) {
        if (!std::isnan(series.at(i)))
            return series.at(i);
    }
    return Missing;
}

void RrdHistory::refresh()
{
    if (!isValid() || m_loading)
        return;
    setLoading(true);
    const int generation = m_generation;
    QPointer<RrdHistory> self(this);
    PveClient::FormFields query;
    query << qMakePair(QStringLiteral("timeframe"), m_timeframe)
          << qMakePair(QStringLiteral("cf"), QStringLiteral("AVERAGE"));
    m_client->get(path(), [self, generation](const PveClient::Reply &reply) {
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

void RrdHistory::update(const QJsonArray &data)
{
    QVector<qint64> times;
    QHash<QString, QVector<double> > series;
    times.reserve(data.size());
    for (const QJsonValue &v : data) {
        const QJsonObject o = v.toObject();
        if (!o.contains(QStringLiteral("time")))
            continue;
        const int index = times.size();
        times.append(static_cast<qint64>(o.value(QStringLiteral("time")).toDouble()));
        for (QJsonObject::const_iterator it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.key() == QLatin1String("time") || !it.value().isDouble())
                continue;
            QVector<double> &values = series[it.key()];
            // Fill skipped samples of this series with gaps.
            while (values.size() < index)
                values.append(Missing);
            values.append(it.value().toDouble());
        }
        // Series without a value in this sample get a gap.
        for (QHash<QString, QVector<double> >::iterator it = series.begin(); it != series.end(); ++it) {
            while (it.value().size() < times.size())
                it.value().append(Missing);
        }
    }
    m_times = times;
    m_series = series;
    m_loaded = true;
    emit historyChanged();
}

QString RrdHistory::path() const
{
    if (m_type.isEmpty())
        return QStringLiteral("/nodes/%1/rrddata").arg(m_node);
    return QStringLiteral("/nodes/%1/%2/%3/rrddata").arg(m_node, m_type).arg(m_vmid);
}

bool RrdHistory::isValid() const
{
    return m_client && m_client->isConnected() && !m_node.isEmpty()
            && (m_type.isEmpty() || m_vmid > 0);
}

void RrdHistory::scheduleReset()
{
    // QML sets the properties one at a time: reset once.
    if (m_resetPending)
        return;
    m_resetPending = true;
    QTimer::singleShot(0, this, [this]() { reset(); });
}

void RrdHistory::reset()
{
    m_resetPending = false;
    ++m_generation;
    setLoading(false);
    setErrorString(QString());
    if (m_loaded || !m_times.isEmpty()) {
        m_times.clear();
        m_series.clear();
        m_loaded = false;
        emit historyChanged();
    }
    updateTimer();
    refresh();
}

void RrdHistory::updateTimer()
{
    if (m_pollInterval > 0 && isValid()) {
        if (m_timer.interval() != m_pollInterval || !m_timer.isActive())
            m_timer.start(m_pollInterval);
    } else {
        m_timer.stop();
    }
}

void RrdHistory::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void RrdHistory::setErrorString(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorStringChanged();
}
