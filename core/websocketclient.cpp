// SPDX-License-Identifier: GPL-3.0-or-later
#include "websocketclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include "formencoding.h"

namespace {

const QByteArray Guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
// Headers and frames larger than this mean something is wrong.
const qint64 MaxHandshake = 16 * 1024;
const quint64 MaxFrame = 16 * 1024 * 1024;
// How long to wait for the server to end the connection after a close.
const int CloseTimeoutMs = 5000;

// std::random_device can be a fixed-seed generator in some C++ libraries.
QByteArray randomBytes(int n)
{
    QFile urandom(QStringLiteral("/dev/urandom"));
    if (urandom.open(QIODevice::ReadOnly)) {
        const QByteArray out = urandom.read(n);
        if (out.size() == n)
            return out;
    }
    // Not expected on Linux; the key only needs to be unpredictable.
    QByteArray seed = QByteArray::number(QDateTime::currentMSecsSinceEpoch())
            + QByteArray::number(qrand());
    QByteArray out;
    while (out.size() < n) {
        seed = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
        out += seed;
    }
    return out.left(n);
}

} // namespace

WebSocketClient::WebSocketClient(QObject *parent)
    : QObject(parent)
{
}

WebSocketClient::~WebSocketClient()
{
    if (m_socket)
        m_socket->abort();
}

WebSocketClient::State WebSocketClient::state() const
{
    return m_state;
}

void WebSocketClient::open(const QUrl &url, const Headers &headers, const QByteArray &protocol,
                           const QString &pinnedFingerprint)
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_url = url;
    m_headers = headers;
    m_protocol = protocol;
    m_pinned = pinnedFingerprint;
    m_buffer.clear();
    m_message.clear();
    m_messageOpcode = 0;
    m_closedEmitted = false;
    m_state = Connecting;

    const bool tls = url.scheme() == QLatin1String("wss");
    const int port = url.port(tls ? 443 : 80);
    QSslSocket *ssl = nullptr;
    if (tls) {
        ssl = new QSslSocket(this);
        m_socket = ssl;
        connect(ssl, &QSslSocket::encrypted, this, &WebSocketClient::onConnected);
        connect(ssl, static_cast<void (QSslSocket::*)(const QList<QSslError> &)>(&QSslSocket::sslErrors),
                this, &WebSocketClient::onSslErrors);
    } else {
        m_socket = new QTcpSocket(this);
        connect(m_socket.data(), &QTcpSocket::connected, this, &WebSocketClient::onConnected);
    }
    QTcpSocket *socket = m_socket;
    connect(socket, &QTcpSocket::readyRead, this, &WebSocketClient::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, [this]() {
        fail(m_state == Connecting ? tr("The connection was closed") : QString());
    });
    connect(socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            this, [this, socket](QAbstractSocket::SocketError error) {
        // The server closing the connection after a close frame is normal.
        if (error == QAbstractSocket::RemoteHostClosedError && m_state != Connecting)
            fail(QString());
        else
            fail(socket->errorString());
    });
    // Connect only now: a failure can be reported before connectToHost returns.
    if (ssl)
        ssl->connectToHostEncrypted(url.host(), port);
    else
        socket->connectToHost(url.host(), port);
}

void WebSocketClient::sendText(const QByteArray &utf8)
{
    send(Text, utf8);
}

void WebSocketClient::sendBinary(const QByteArray &data)
{
    send(Binary, data);
}

void WebSocketClient::close()
{
    if (m_state == Open) {
        send(Close, QByteArray());
        m_state = Closing;
        if (m_socket)
            m_socket->disconnectFromHost();
        startCloseTimeout();
    } else if (m_state == Connecting) {
        fail(QString());
    }
}

QByteArray WebSocketClient::acceptKey(const QByteArray &key)
{
    return QCryptographicHash::hash(key + Guid, QCryptographicHash::Sha1).toBase64();
}

QByteArray WebSocketClient::frame(int opcode, const QByteArray &payload, bool masked)
{
    QByteArray out;
    out.append(static_cast<char>(0x80 | (opcode & 0x0f)));
    const quint64 len = static_cast<quint64>(payload.size());
    const char maskBit = masked ? char(0x80) : char(0);
    if (len < 126) {
        out.append(static_cast<char>(maskBit | static_cast<char>(len)));
    } else if (len <= 0xffff) {
        out.append(static_cast<char>(maskBit | 126));
        uchar be[2];
        qToBigEndian<quint16>(static_cast<quint16>(len), be);
        out.append(reinterpret_cast<const char *>(be), 2);
    } else {
        out.append(static_cast<char>(maskBit | 127));
        uchar be[8];
        qToBigEndian<quint64>(len, be);
        out.append(reinterpret_cast<const char *>(be), 8);
    }
    if (!masked)
        return out + payload;

    const QByteArray mask = randomBytes(4);
    out.append(mask);
    QByteArray body = payload;
    for (int i = 0; i < body.size(); ++i)
        body[i] = static_cast<char>(body.at(i) ^ mask.at(i % 4));
    return out + body;
}

void WebSocketClient::onConnected()
{
    m_key = randomBytes(16).toBase64();
    QByteArray path = m_url.path(QUrl::FullyEncoded).toLatin1();
    if (path.isEmpty())
        path = "/";
    if (m_url.hasQuery())
        path += '?' + m_url.query(QUrl::FullyEncoded).toLatin1();

    QByteArray request = "GET " + path + " HTTP/1.1\r\n";
    const QByteArray host = m_url.host(QUrl::FullyEncoded).toLatin1();
    request += "Host: " + (host.contains(':') ? '[' + host + ']' : host);
    if (m_url.port() > 0)
        request += ':' + QByteArray::number(m_url.port());
    request += "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + m_key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    if (!m_protocol.isEmpty())
        request += "Sec-WebSocket-Protocol: " + m_protocol + "\r\n";
    foreach (const Headers::value_type &h, m_headers)
        request += h.first + ": " + h.second + "\r\n";
    request += "\r\n";
    m_socket->write(request);
}

void WebSocketClient::onReadyRead()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    if (m_state == Connecting && !parseHandshake())
        return;
    if (m_state == Open || m_state == Closing)
        parseFrames();
}

void WebSocketClient::onSslErrors(const QList<QSslError> &errors)
{
    QSslSocket *ssl = qobject_cast<QSslSocket *>(m_socket.data());
    if (!ssl)
        return;
    const QString fingerprint = Pve::certificateFingerprint(ssl->peerCertificate());
    if (!fingerprint.isEmpty() && fingerprint == m_pinned)
        ssl->ignoreSslErrors(errors);
    // Otherwise the socket aborts and error() reports why.
}

bool WebSocketClient::parseHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_buffer.size() > MaxHandshake)
            fail(tr("Invalid WebSocket handshake"));
        return false;
    }
    const QList<QByteArray> lines = m_buffer.left(end).split('\n');
    m_buffer.remove(0, end + 4);

    const QList<QByteArray> status = lines.value(0).trimmed().split(' ');
    const int code = status.value(1).toInt();
    if (code != 101) {
        QByteArray reason = lines.value(0).trimmed();
        reason = reason.mid(reason.indexOf(' ', reason.indexOf(' ') + 1) + 1);
        fail(code == 401 ? tr("Permission denied")
                         : tr("The console was refused: %1 %2").arg(code).arg(QString::fromUtf8(reason)));
        return false;
    }
    QByteArray accept;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "sec-websocket-accept")
            accept = line.mid(colon + 1).trimmed();
    }
    if (accept != acceptKey(m_key)) {
        fail(tr("Invalid WebSocket handshake"));
        return false;
    }
    m_state = Open;
    emit opened();
    return true;
}

void WebSocketClient::parseFrames()
{
    while (m_socket && (m_state == Open || m_state == Closing)) {
        if (m_buffer.size() < 2)
            return;
        const uchar b0 = static_cast<uchar>(m_buffer.at(0));
        const uchar b1 = static_cast<uchar>(m_buffer.at(1));
        const bool fin = b0 & 0x80;
        const int opcode = b0 & 0x0f;
        const bool masked = b1 & 0x80;
        // No extensions were negotiated, and servers must not mask.
        if ((b0 & 0x70) || masked) {
            fail(tr("Invalid WebSocket frame"));
            return;
        }
        // Control frames are short and never fragmented.
        if ((opcode & 0x8) && (!fin || (b1 & 0x7f) > 125)) {
            fail(tr("Invalid WebSocket frame"));
            return;
        }
        quint64 len = b1 & 0x7f;
        int header = 2;
        if (len == 126) {
            if (m_buffer.size() < 4)
                return;
            len = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(m_buffer.constData() + 2));
            header = 4;
        } else if (len == 127) {
            if (m_buffer.size() < 10)
                return;
            len = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(m_buffer.constData() + 2));
            header = 10;
        }
        if (len > MaxFrame) {
            fail(tr("WebSocket frame too large"));
            return;
        }
        const int maskLen = masked ? 4 : 0;
        if (static_cast<quint64>(m_buffer.size()) < header + maskLen + len)
            return;

        QByteArray payload = m_buffer.mid(header + maskLen, static_cast<int>(len));
        if (masked) {
            const QByteArray mask = m_buffer.mid(header, 4);
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<char>(payload.at(i) ^ mask.at(i % 4));
        }
        m_buffer.remove(0, header + maskLen + static_cast<int>(len));

        switch (opcode) {
        case Ping:
            send(Pong, payload);
            break;
        case Pong:
            break;
        case Close:
            if (m_state == Open)
                send(Close, payload.left(2));
            m_state = Closing;
            if (m_socket)
                m_socket->disconnectFromHost();
            startCloseTimeout();
            return;
        case Text:
        case Binary:
        case Continuation:
            // A continuation needs a message to continue, and a new message
            // can't start inside another one.
            if ((opcode == Continuation) != (m_messageOpcode != 0)) {
                fail(tr("Invalid WebSocket frame"));
                return;
            }
            if (opcode != Continuation)
                m_messageOpcode = opcode;
            if (static_cast<quint64>(m_message.size() + payload.size()) > MaxFrame) {
                fail(tr("WebSocket message too large"));
                return;
            }
            m_message += payload;
            if (fin) {
                const QByteArray message = m_message;
                const bool text = m_messageOpcode == Text;
                m_message.clear();
                m_messageOpcode = 0;
                emit messageReceived(message, text);
            }
            break;
        default:
            fail(tr("Unknown WebSocket frame"));
            return;
        }
    }
}

void WebSocketClient::send(int opcode, const QByteArray &payload)
{
    if (!m_socket || (m_state != Open && !(m_state == Closing && opcode == Close)))
        return;
    m_socket->write(frame(opcode, payload, true));
}

void WebSocketClient::startCloseTimeout()
{
    // A server that never ends the connection must not keep it open.
    QTimer::singleShot(CloseTimeoutMs, this, [this]() {
        if (m_state == Closing)
            fail(QString());
    });
}

void WebSocketClient::fail(const QString &error)
{
    if (m_closedEmitted)
        return;
    m_closedEmitted = true;
    m_state = Closed;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    emit closed(error);
}
