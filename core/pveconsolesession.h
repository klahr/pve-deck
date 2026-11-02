// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_PVECONSOLESESSION_H
#define PVEDECK_PVECONSOLESESSION_H

#include <QPointer>
#include <QTimer>

#include "pveclient.h"
#include "terminalbackend.h"

class WebSocketClient;

// A Proxmox text console (termproxy) over a WebSocket:
//   1. POST /nodes/{node}[/{type}/{vmid}]/termproxy  ->  {port, ticket, user}
//   2. WebSocket .../vncwebsocket?port=..&vncticket=..  with the auth cookie
//   3. "<user>:<ticket>\n", server answers "OK", then terminal bytes
// type is "lxc", "qemu" (serial console) or empty for the node's shell.
class PveConsoleSession : public TerminalBackend
{
    Q_OBJECT
    Q_PROPERTY(PveClient *client READ client WRITE setClient NOTIFY targetChanged)
    Q_PROPERTY(QString node READ node WRITE setNode NOTIFY targetChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY targetChanged)
    Q_PROPERTY(int vmid READ vmid WRITE setVmid NOTIFY targetChanged)

public:
    explicit PveConsoleSession(QObject *parent = nullptr);
    ~PveConsoleSession();

    PveClient *client() const;
    void setClient(PveClient *client);
    QString node() const;
    void setNode(const QString &node);
    QString type() const;
    void setType(const QString &type);
    int vmid() const;
    void setVmid(int vmid);

    // Keep-alive interval in ms, adjustable for tests. Default 30 s.
    void setPingInterval(int ms);

    void open() override;
    void close() override;
    void write(const QByteArray &data) override;
    void resize(int columns, int rows) override;

signals:
    void targetChanged();

private:
    QString targetPath() const;
    void onOpened();
    void onMessage(const QByteArray &data);
    void onClosed(const QString &error);
    void sendText(const QByteArray &text);

    QPointer<PveClient> m_client;
    QString m_node;
    QString m_type;
    int m_vmid = 0;

    WebSocketClient *m_socket = nullptr;
    QTimer m_ping;
    int m_generation = 0;
    QString m_user;
    QString m_ticket;
    bool m_awaitingOk = false;
    int m_columns = 0;
    int m_rows = 0;
    QByteArray m_pendingInput;
};

#endif // PVEDECK_PVECONSOLESESSION_H
