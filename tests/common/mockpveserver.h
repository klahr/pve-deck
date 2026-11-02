// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_MOCKPVESERVER_H
#define PVEDECK_MOCKPVESERVER_H

#include <QByteArray>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

// Minimal HTTP/1.1 server that answers like the Proxmox VE API.
// One request per connection ("Connection: close").
class MockPveServer : public QTcpServer
{
    Q_OBJECT

public:
    struct Request {
        QByteArray method;
        QString path;
        QHash<QString, QString> query;
        QHash<QByteArray, QByteArray> headers;   // lower-case names
        QHash<QString, QString> form;
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray reason = "OK";
        QJsonValue data;
        static Response ok(const QJsonValue &data) { Response r; r.data = data; return r; }
        static Response error(int status, const QByteArray &reason)
        {
            Response r;
            r.status = status;
            r.reason = reason;
            r.data = QJsonValue::Null;
            return r;
        }
    };

    typedef std::function<Response(const Request &)> Handler;

    explicit MockPveServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, &MockPveServer::onNewConnection);
        listen(QHostAddress::LocalHost, 0);
    }

    Handler handler;
    QList<Request> requests;

    QList<Request> requestsTo(const QString &path) const
    {
        QList<Request> out;
        foreach (const Request &r, requests)
            if (r.path == path)
                out.append(r);
        return out;
    }

private:
    static QHash<QString, QString> parseForm(const QByteArray &encoded)
    {
        QHash<QString, QString> out;
        foreach (const QByteArray &pair, encoded.split('&')) {
            if (pair.isEmpty())
                continue;
            const int eq = pair.indexOf('=');
            const QByteArray key = eq < 0 ? pair : pair.left(eq);
            const QByteArray value = eq < 0 ? QByteArray() : pair.mid(eq + 1);
            out.insert(QUrl::fromPercentEncoding(QByteArray(key).replace('+', ' ')),
                       QUrl::fromPercentEncoding(QByteArray(value).replace('+', ' ')));
        }
        return out;
    }

    void onNewConnection()
    {
        while (QTcpSocket *socket = nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void onReadyRead(QTcpSocket *socket)
    {
        QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
        socket->setProperty("buffer", buffer);

        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        Request req;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        req.method = requestLine.value(0);
        const QUrl url(QString::fromLatin1(requestLine.value(1)));
        req.path = url.path();
        req.query = parseForm(url.query(QUrl::FullyEncoded).toLatin1());
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon > 0)
                req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        const int length = req.headers.value("content-length").toInt();
        if (buffer.size() < headerEnd + 4 + length)
            return;
        req.body = buffer.mid(headerEnd + 4, length);
        req.form = parseForm(req.body);
        requests.append(req);

        const Response resp = handler ? handler(req) : Response::error(501, "not implemented");
        QJsonObject root;
        root.insert(QStringLiteral("data"), resp.data);
        const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

        QByteArray out = "HTTP/1.1 " + QByteArray::number(resp.status) + ' ' + resp.reason + "\r\n";
        out += "Content-Type: application/json;charset=UTF-8\r\n";
        out += "Content-Length: " + QByteArray::number(json.size()) + "\r\n";
        out += "Connection: close\r\n\r\n";
        out += json;
        socket->write(out);
        socket->disconnectFromHost();
    }
};

#endif // PVEDECK_MOCKPVESERVER_H
