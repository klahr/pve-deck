// SPDX-License-Identifier: GPL-3.0-or-later
#include "pveconsolesession.h"

#include <QJsonObject>

#include "consoleprotocol.h"
#include "formencoding.h"
#include "websocketclient.h"

namespace {
// Input typed before the console is ready is kept, up to this much.
const int MaxPendingInput = 4096;
}

PveConsoleSession::PveConsoleSession(QObject *parent)
    : TerminalBackend(parent)
{
    m_ping.setInterval(30000);
    connect(&m_ping, &QTimer::timeout, this, [this]() { sendText(ConsoleProtocol::ping()); });
}

PveConsoleSession::~PveConsoleSession()
{
    // Don't emit signals into a half-destroyed QML scene.
    if (m_socket)
        m_socket->disconnect(this);
}

PveClient *PveConsoleSession::client() const
{
    return m_client;
}

void PveConsoleSession::setClient(PveClient *client)
{
    if (client == m_client)
        return;
    m_client = client;
    emit targetChanged();
}

QString PveConsoleSession::node() const
{
    return m_node;
}

void PveConsoleSession::setNode(const QString &node)
{
    if (node == m_node)
        return;
    m_node = node;
    emit targetChanged();
}

QString PveConsoleSession::type() const
{
    return m_type;
}

void PveConsoleSession::setType(const QString &type)
{
    if (type == m_type)
        return;
    m_type = type;
    emit targetChanged();
}

int PveConsoleSession::vmid() const
{
    return m_vmid;
}

void PveConsoleSession::setVmid(int vmid)
{
    if (vmid == m_vmid)
        return;
    m_vmid = vmid;
    emit targetChanged();
}

void PveConsoleSession::setPingInterval(int ms)
{
    m_ping.setInterval(ms);
}

void PveConsoleSession::open()
{
    if (state() == Connecting || state() == Connected)
        return;
    if (!m_client || !m_client->isConnected()) {
        setState(Closed, tr("Not logged in"));
        return;
    }
    if (m_node.isEmpty() || (!m_type.isEmpty() && m_vmid <= 0)) {
        setState(Closed, tr("No console selected"));
        return;
    }
    const int generation = ++m_generation;
    m_awaitingOk = false;
    setState(Connecting);

    QPointer<PveConsoleSession> self(this);
    m_client->post(targetPath() + QStringLiteral("/termproxy"), [self, generation](const PveClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        if (!reply.ok) {
            self->setState(Closed, reply.error);
            return;
        }
        PveClient *client = self->m_client;
        if (!client) {
            self->setState(Closed, tr("Not logged in"));
            return;
        }
        const QJsonObject o = reply.data.toObject();
        // port is a number in newer PVE versions and a string in older ones.
        const QString port = o.value(QStringLiteral("port")).toVariant().toString();
        self->m_user = o.value(QStringLiteral("user")).toString();
        self->m_ticket = o.value(QStringLiteral("ticket")).toString();
        if (port.isEmpty() || self->m_ticket.isEmpty()) {
            self->setState(Closed, tr("The server did not start a console"));
            return;
        }

        QUrl url = client->baseUrl();
        url.setScheme(client->isTlsEnabled() ? QStringLiteral("wss") : QStringLiteral("ws"));
        url.setPath(QStringLiteral("/api2/json") + self->targetPath() + QStringLiteral("/vncwebsocket"));
        Pve::FormFields query;
        query << qMakePair(QStringLiteral("port"), port)
              << qMakePair(QStringLiteral("vncticket"), self->m_ticket);
        url.setQuery(QString::fromLatin1(Pve::formEncode(query)), QUrl::StrictMode);

        if (self->m_socket)
            self->m_socket->deleteLater();
        self->m_socket = new WebSocketClient(self);
        connect(self->m_socket, &WebSocketClient::opened, self, &PveConsoleSession::onOpened);
        connect(self->m_socket, &WebSocketClient::messageReceived, self,
                [self](const QByteArray &data, bool) { self->onMessage(data); });
        connect(self->m_socket, &WebSocketClient::closed, self, &PveConsoleSession::onClosed);
        self->m_socket->open(url, client->authHeaders(), QByteArrayLiteral("binary"),
                             client->pinnedFingerprint());
    });
}

void PveConsoleSession::close()
{
    ++m_generation;
    m_ping.stop();
    m_pendingInput.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (state() != Idle)
        setState(Closed);
}

void PveConsoleSession::write(const QByteArray &data)
{
    if (state() == Connected) {
        sendText(ConsoleProtocol::input(data));
    } else if (state() == Connecting && m_pendingInput.size() + data.size() <= MaxPendingInput) {
        m_pendingInput += data;
    }
}

void PveConsoleSession::resize(int columns, int rows)
{
    if (columns <= 0 || rows <= 0 || (columns == m_columns && rows == m_rows))
        return;
    m_columns = columns;
    m_rows = rows;
    if (state() == Connected)
        sendText(ConsoleProtocol::resize(columns, rows));
}

QString PveConsoleSession::targetPath() const
{
    if (m_type.isEmpty())
        return QStringLiteral("/nodes/%1").arg(m_node);
    return QStringLiteral("/nodes/%1/%2/%3").arg(m_node, m_type).arg(m_vmid);
}

void PveConsoleSession::onOpened()
{
    m_awaitingOk = true;
    sendText(ConsoleProtocol::login(m_user, m_ticket));
}

void PveConsoleSession::onMessage(const QByteArray &data)
{
    if (!m_awaitingOk) {
        emit dataReceived(data);
        return;
    }
    if (!data.startsWith("OK")) {
        const QString message = QString::fromUtf8(data).trimmed();
        close();
        setState(Closed, message.isEmpty() ? tr("The console was refused") : message);
        return;
    }
    m_awaitingOk = false;
    setState(Connected);
    m_ping.start();
    if (m_columns > 0 && m_rows > 0)
        sendText(ConsoleProtocol::resize(m_columns, m_rows));
    if (!m_pendingInput.isEmpty()) {
        sendText(ConsoleProtocol::input(m_pendingInput));
        m_pendingInput.clear();
    }
    if (data.size() > 2)
        emit dataReceived(data.mid(2));
}

void PveConsoleSession::onClosed(const QString &error)
{
    m_ping.stop();
    m_pendingInput.clear();
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    setState(Closed, error);
}

void PveConsoleSession::sendText(const QByteArray &text)
{
    if (m_socket)
        m_socket->sendText(text);
}
