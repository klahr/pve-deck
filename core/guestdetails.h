// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_GUESTDETAILS_H
#define PVEDECK_GUESTDETAILS_H

#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "pveclient.h"

// Live status of one guest:
//   GET /nodes/{node}/{type}/{vmid}/status/current
// plus its IP addresses, from the QEMU guest agent
// (agent/network-get-interfaces) or the LXC interfaces call. Addresses
// are fetched on refresh() and whenever the guest starts running;
// the status is polled every pollInterval ms.
class GuestDetails : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PveClient *client READ client WRITE setClient NOTIFY guestChanged)
    Q_PROPERTY(QString node READ node WRITE setNode NOTIFY guestChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY guestChanged)
    Q_PROPERTY(int vmid READ vmid WRITE setVmid NOTIFY guestChanged)
    Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval NOTIFY pollIntervalChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList addresses READ addresses NOTIFY addressesChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit GuestDetails(QObject *parent = nullptr);

    PveClient *client() const;
    void setClient(PveClient *client);
    QString node() const;
    void setNode(const QString &node);
    QString type() const;
    void setType(const QString &type);
    int vmid() const;
    void setVmid(int vmid);
    int pollInterval() const;
    void setPollInterval(int ms);

    QVariantMap status() const;
    QStringList addresses() const;
    bool isLoading() const;
    bool isLoaded() const;
    QString errorString() const;

    Q_INVOKABLE void refresh();

    // Addresses without loopback and IPv6 link-local. Public for tests.
    static QStringList parseAgentAddresses(const QJsonValue &data);
    static QStringList parseLxcAddresses(const QJsonValue &data);

signals:
    void guestChanged();
    void pollIntervalChanged();
    void statusChanged();
    void addressesChanged();
    void loadingChanged();
    void errorStringChanged();

private:
    QString basePath() const;
    bool isValid() const;
    void fetchStatus(bool withAddresses);
    void fetchAddresses();
    void scheduleReset();
    void guestReset();
    void updateTimer();
    void setLoading(bool loading);
    void setErrorString(const QString &error);
    void setAddresses(const QStringList &addresses);

    QPointer<PveClient> m_client;
    QString m_node;
    QString m_type;
    int m_vmid = 0;
    int m_pollInterval = 0;
    int m_generation = 0;
    QTimer m_timer;
    QVariantMap m_status;
    QStringList m_addresses;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_resetPending = false;
    QString m_error;
};

#endif // PVEDECK_GUESTDETAILS_H
