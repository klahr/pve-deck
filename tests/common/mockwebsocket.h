// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_MOCKWEBSOCKET_H
#define PVEDECK_MOCKWEBSOCKET_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QList>
#include <QObject>
#include <QTcpSocket>
#include <QtEndian>

#include <functional>

// The server side of one WebSocket connection, for tests: answers the
// upgrade, unmasks client frames into messages and sends unmasked frames.
class MockWebSocket : public QObject
{
    Q_OBJECT

public:
    MockWebSocket(QTcpSocket *socket, const QByteArray &key, const QByteArray &protocol,
                  QObject *parent = nullptr)
        : QObject(parent)
        , m_socket(socket)
    {
        socket->setParent(this);
        const QByteArray accept = QCryptographicHash::hash(
                    key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
        QByteArray out = "HTTP/1.1 101 Switching Protocols\r\n"
                         "Upgrade: websocket\r\nConnection: Upgrade\r\n";
        out += "Sec-WebSocket-Accept: " + accept + "\r\n";
        if (!protocol.isEmpty())
            out += "Sec-WebSocket-Protocol: " + protocol + "\r\n";
        out += "\r\n";
        socket->write(out);
        m_buffer = socket->property("buffer").toByteArray();
        connect(socket, &QTcpSocket::readyRead, this, [this]() {
            m_buffer += m_socket->readAll();
            parse();
        });
        parse();
    }

    QList<QByteArray> messages;
    bool allMasked = true;
    bool closeReceived = false;
    std::function<void(const QByteArray &)> onMessage;

    void sendText(const QByteArray &data) { send(0x1, data); }
    void sendBinary(const QByteArray &data) { send(0x2, data); }
    void sendPing(const QByteArray &data) { send(0x9, data); }
    void sendClose() { send(0x8, QByteArray("\x03\xe8", 2)); }

    QList<QByteArray> pongs;

private:
    void send(int opcode, const QByteArray &payload)
    {
        QByteArray out;
        out.append(static_cast<char>(0x80 | opcode));
        if (payload.size() < 126) {
            out.append(static_cast<char>(payload.size()));
        } else {
            out.append(static_cast<char>(126));
            uchar be[2];
            qToBigEndian<quint16>(static_cast<quint16>(payload.size()), be);
            out.append(reinterpret_cast<const char *>(be), 2);
        }
        m_socket->write(out + payload);
    }

    void parse()
    {
        while (m_buffer.size() >= 2) {
            const int opcode = static_cast<uchar>(m_buffer.at(0)) & 0x0f;
            const bool masked = static_cast<uchar>(m_buffer.at(1)) & 0x80;
            int len = static_cast<uchar>(m_buffer.at(1)) & 0x7f;
            int header = 2;
            if (len == 126) {
                if (m_buffer.size() < 4)
                    return;
                len = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(m_buffer.constData() + 2));
                header = 4;
            }
            const int maskLen = masked ? 4 : 0;
            if (m_buffer.size() < header + maskLen + len)
                return;
            QByteArray payload = m_buffer.mid(header + maskLen, len);
            if (masked) {
                const QByteArray mask = m_buffer.mid(header, 4);
                for (int i = 0; i < payload.size(); ++i)
                    payload[i] = static_cast<char>(payload.at(i) ^ mask.at(i % 4));
            } else {
                allMasked = false;
            }
            m_buffer.remove(0, header + maskLen + len);
            if (opcode == 0x8) {
                closeReceived = true;
                m_socket->disconnectFromHost();
                return;
            }
            if (opcode == 0xA) {
                pongs.append(payload);
                continue;
            }
            messages.append(payload);
            if (onMessage)
                onMessage(payload);
        }
    }

    QTcpSocket *m_socket;
    QByteArray m_buffer;
};

#endif // PVEDECK_MOCKWEBSOCKET_H
