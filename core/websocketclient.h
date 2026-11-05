// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_WEBSOCKETCLIENT_H
#define PVEDECK_WEBSOCKETCLIENT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSslError>
#include <QUrl>

class QTcpSocket;

// Minimal RFC 6455 WebSocket client.
//
// QtWebSockets 5.5 (what Sailfish OS ships) can't send request headers,
// but the Proxmox console needs the PVEAuthCookie or an API token
// header. This client sends any headers and accepts a self-signed
// certificate only if its SHA-256 fingerprint matches the pinned one,
// like PveClient. No extensions; fragmented messages are reassembled.
class WebSocketClient : public QObject
{
    Q_OBJECT

public:
    typedef QList<QPair<QByteArray, QByteArray> > Headers;

    enum State { Closed, Connecting, Open, Closing };

    explicit WebSocketClient(QObject *parent = nullptr);
    ~WebSocketClient();

    State state() const;

    // url is ws:// or wss://. pinnedFingerprint may be empty to accept
    // only certificates the system trusts.
    void open(const QUrl &url, const Headers &headers = Headers(),
              const QByteArray &protocol = QByteArray(),
              const QString &pinnedFingerprint = QString());
    void sendText(const QByteArray &utf8);
    void sendBinary(const QByteArray &data);
    void close();

    // Exposed for tests.
    static QByteArray acceptKey(const QByteArray &key);
    static QByteArray frame(int opcode, const QByteArray &payload, bool masked);

signals:
    void opened();
    void messageReceived(const QByteArray &data, bool isText);
    // Emitted once after opened(), or instead of it if the handshake fails.
    void closed(const QString &error);

private:
    enum Opcode { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

    void onConnected();
    void onReadyRead();
    void onSslErrors(const QList<QSslError> &errors);
    bool parseHandshake();
    void parseFrames();
    void send(int opcode, const QByteArray &payload);
    void startCloseTimeout();
    void fail(const QString &error);

    QPointer<QTcpSocket> m_socket;
    State m_state = Closed;
    QUrl m_url;
    Headers m_headers;
    QByteArray m_protocol;
    QString m_pinned;
    QByteArray m_key;
    QByteArray m_buffer;
    QByteArray m_message;
    int m_messageOpcode = 0;    // of the message being reassembled, 0 if none
    bool m_closedEmitted = false;
};

#endif // PVEDECK_WEBSOCKETCLIENT_H
