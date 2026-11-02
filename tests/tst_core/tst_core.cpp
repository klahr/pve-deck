// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSettings>
#include <cmath>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "clusteroverview.h"
#include "consoleprotocol.h"
#include "formencoding.h"
#include "guestdetails.h"
#include "memorysecretbackend.h"
#include "mockpveserver.h"
#include "mockwebsocket.h"
#include "poweractions.h"
#include "pveclient.h"
#include "pveconsolesession.h"
#include "resourcefiltermodel.h"
#include "resourcemodel.h"
#include "rrdhistory.h"
#include "serverprofile.h"
#include "serverstore.h"
#include "sessionmanager.h"
#include "taskwatcher.h"
#include "terminalscreen.h"
#include "tfachallenge.h"
#include "websocketclient.h"

namespace {

const QString Ticket1 = QStringLiteral("PVE:root@pam:65F00001::sig+/=1");
const QString Ticket2 = QStringLiteral("PVE:root@pam:65F00002::sig+/=2");
const QString TfaTicket = QStringLiteral("PVE:!tfa!%7B%22totp%22%3Atrue%2C%22recovery%22%3A%5B0%2C2%5D%2C%22yubico%22%3Atrue%7D:65F00000::c2ln");
const QString RecoveryKey = QStringLiteral("0a1b-2c3d-4e5f-6a7b");
const QString TokenSecret = QStringLiteral("8a4b6c1e-0000-4000-8000-123456789abc");

QJsonObject ticketData(const QString &ticket, const QString &csrf, const QString &user)
{
    QJsonObject o;
    o.insert(QStringLiteral("ticket"), ticket);
    o.insert(QStringLiteral("CSRFPreventionToken"), csrf);
    o.insert(QStringLiteral("username"), user);
    return o;
}

QJsonObject guest(int vmid, const QString &name, const QString &node, const QString &type,
                  const QString &status, bool isTemplate = false, const QString &tags = QString())
{
    QJsonObject o;
    o.insert(QStringLiteral("vmid"), vmid);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("node"), node);
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("status"), status);
    o.insert(QStringLiteral("cpu"), status == QLatin1String("running") ? 0.25 : 0.0);
    o.insert(QStringLiteral("maxcpu"), 2);
    o.insert(QStringLiteral("mem"), status == QLatin1String("running") ? 536870912.0 : 0.0);
    o.insert(QStringLiteral("maxmem"), 2147483648.0);
    o.insert(QStringLiteral("template"), isTemplate ? 1 : 0);
    if (!tags.isEmpty())
        o.insert(QStringLiteral("tags"), tags);
    return o;
}

QJsonObject nodeResource(const QString &name, const QString &status, double cpu)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("node"));
    o.insert(QStringLiteral("node"), name);
    o.insert(QStringLiteral("status"), status);
    o.insert(QStringLiteral("cpu"), cpu);
    o.insert(QStringLiteral("maxcpu"), 8);
    o.insert(QStringLiteral("mem"), 4294967296.0);
    o.insert(QStringLiteral("maxmem"), 17179869184.0);
    o.insert(QStringLiteral("uptime"), 86400);
    return o;
}

QJsonObject nodeStatus(const QString &name, bool online, bool local, const QString &ip)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("node"));
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("online"), online ? 1 : 0);
    o.insert(QStringLiteral("local"), local ? 1 : 0);
    o.insert(QStringLiteral("ip"), ip);
    return o;
}

QJsonObject clusterEntry(const QString &name, bool quorate)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("cluster"));
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("quorate"), quorate ? 1 : 0);
    return o;
}

QStringList nodeNames(const QAbstractItemModel &model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out.append(model.index(i, 0).data(ClusterOverview::NameRole).toString());
    return out;
}

QList<int> vmids(const QAbstractItemModel &model)
{
    QList<int> out;
    for (int i = 0; i < model.rowCount(); ++i)
        out.append(model.index(i, 0).data(ResourceModel::VmidRole).toInt());
    return out;
}

QString cookieTicket(const MockPveServer::Request &r)
{
    const QByteArray cookie = r.headers.value("cookie");
    const QByteArray prefix = "PVEAuthCookie=";
    if (!cookie.startsWith(prefix))
        return QString();
    return QUrl::fromPercentEncoding(cookie.mid(prefix.size()));
}

// Behaves like a PVE 8 node with two users:
//   root@pam / "secret"          no 2FA
//   tfa@pve  / "secret"          TOTP, valid code 123456
// and an API token root@pam!phone.
struct FakePve
{
    QString currentTicket;
    QString currentCsrf;
    int ticketCounter = 0;
    QJsonArray resources;
    QJsonArray clusterStatus;
    bool clusterStatusAllowed = true;
    QHash<QString, int> taskPolls;
    // What status/current says about guest 100; /cluster/resources may lag.
    QString guestStatus = QStringLiteral("running");

    MockPveServer::Response handle(const MockPveServer::Request &r)
    {
        typedef MockPveServer::Response Resp;

        if (r.path == QLatin1String("/api2/json/access/ticket") && r.method == "POST") {
            const QString user = r.form.value(QStringLiteral("username"));
            const QString password = r.form.value(QStringLiteral("password"));

            if (r.form.contains(QStringLiteral("tfa-challenge"))) {
                if (r.form.value(QStringLiteral("tfa-challenge")) == TfaTicket
                        && (password == QLatin1String("totp:123456")
                            || password == QLatin1String("recovery:") + RecoveryKey))
                    return Resp::ok(issue(user));
                return Resp::error(401, "authentication failure");
            }
            if (!currentTicket.isEmpty() && password == currentTicket)
                return Resp::ok(issue(user));
            if (user == QLatin1String("root@pam") && password == QLatin1String("secret"))
                return Resp::ok(issue(user));
            if (user == QLatin1String("tfa@pve") && password == QLatin1String("secret")) {
                QJsonObject o;
                o.insert(QStringLiteral("NeedTFA"), 1);
                o.insert(QStringLiteral("ticket"), TfaTicket);
                o.insert(QStringLiteral("username"), user);
                return Resp::ok(o);
            }
            return Resp::error(401, "authentication failure");
        }

        if (!authorized(r))
            return Resp::error(401, "No ticket");

        if (r.path == QLatin1String("/api2/json/version")) {
            QJsonObject o;
            o.insert(QStringLiteral("version"), QStringLiteral("8.2.4"));
            return Resp::ok(o);
        }
        if (r.path == QLatin1String("/api2/json/nodes/pve/qemu/100/status/start") && r.method == "POST") {
            if (r.headers.value("authorization").isEmpty()
                    && QString::fromLatin1(r.headers.value("csrfpreventiontoken")) != currentCsrf)
                return Resp::error(401, "Permission denied - invalid csrf token");
            return Resp::ok(QStringLiteral("UPID:pve:0001:qmstart:100:root@pam:"));
        }
        if (r.path == QLatin1String("/api2/json/cluster/resources"))
            return Resp::ok(resources);
        if (r.path == QLatin1String("/api2/json/nodes/pve/qemu/100/rrddata")) {
            const QByteArray json =
                    "[{\"time\": 1000, \"cpu\": 0.5, \"mem\": 100, \"maxmem\": 400},"
                    " {\"time\": 1060},"
                    " {\"time\": 1120, \"cpu\": 0.25, \"mem\": 200, \"maxmem\": 400, \"netin\": 10}]";
            return Resp::ok(QJsonDocument::fromJson(json).array());
        }
        if (r.path == QLatin1String("/api2/json/cluster/options")) {
            QJsonObject o;
            o.insert(QStringLiteral("tag-style"), QStringLiteral("color-map=prod:FF0000:FFFFFF;web:00aa00,shape=circle"));
            return Resp::ok(o);
        }
        if (r.path == QLatin1String("/api2/json/cluster/status")) {
            if (!clusterStatusAllowed)
                return Resp::error(403, "Permission check failed (/, Sys.Audit)");
            return Resp::ok(clusterStatus);
        }
        if (r.path == QLatin1String("/api2/json/nodes/pve/qemu/100/status/current")) {
            QJsonObject o = guest(100, QStringLiteral("web"), QStringLiteral("pve"),
                                  QStringLiteral("qemu"), guestStatus);
            o.insert(QStringLiteral("qmpstatus"), QStringLiteral("running"));
            o.insert(QStringLiteral("cpus"), 2);
            o.insert(QStringLiteral("uptime"), 3600);
            o.insert(QStringLiteral("agent"), 1);
            return Resp::ok(o);
        }
        if (r.path == QLatin1String("/api2/json/nodes/pve/qemu/100/agent/network-get-interfaces")) {
            const QByteArray json =
                    "{\"result\": ["
                    "{\"name\": \"lo\", \"ip-addresses\": [{\"ip-address\": \"127.0.0.1\"}]},"
                    "{\"name\": \"eth0\", \"ip-addresses\": ["
                    "{\"ip-address\": \"192.168.1.10\"},"
                    "{\"ip-address\": \"fe80::5054:ff:fe12:3456\"},"
                    "{\"ip-address\": \"fd00::10\"}]}]}";
            return Resp::ok(QJsonDocument::fromJson(json).object());
        }
        if (r.path == QLatin1String("/api2/json/nodes/pve/lxc/200/termproxy") && r.method == "POST") {
            QJsonObject o;
            o.insert(QStringLiteral("port"), 5900);
            o.insert(QStringLiteral("ticket"), QStringLiteral("PVEVNC:abc+/=:sig"));
            o.insert(QStringLiteral("user"), QStringLiteral("root@pam"));
            o.insert(QStringLiteral("upid"), QStringLiteral("UPID:pve:0004:0005:65F00000:vncproxy:200:root@pam:"));
            return Resp::ok(o);
        }
        // stop succeeds, reset fails (see the task status below).
        if ((r.path == QLatin1String("/api2/json/nodes/pve/qemu/100/status/stop")
             || r.path == QLatin1String("/api2/json/nodes/pve/qemu/100/status/reset")) && r.method == "POST") {
            const QString kind = r.path.endsWith(QLatin1String("stop")) ? QStringLiteral("qmstop") : QStringLiteral("qmreset");
            return Resp::ok(QStringLiteral("UPID:pve:0002:0003:65F00000:%1:100:root@pam:").arg(kind));
        }
        // Each task reports "running" once, then "stopped".
        const QString taskPrefix = QStringLiteral("/api2/json/nodes/pve/tasks/");
        if (r.path.startsWith(taskPrefix) && r.path.endsWith(QLatin1String("/status"))) {
            const QString upid = r.path.mid(taskPrefix.size()).section(QLatin1Char('/'), 0, 0);
            QJsonObject o;
            o.insert(QStringLiteral("upid"), upid);
            if (taskPolls[upid]++ == 0) {
                o.insert(QStringLiteral("status"), QStringLiteral("running"));
            } else {
                o.insert(QStringLiteral("status"), QStringLiteral("stopped"));
                o.insert(QStringLiteral("exitstatus"), upid.contains(QLatin1String("qmreset"))
                         ? QStringLiteral("VM is locked (backup)") : QStringLiteral("OK"));
            }
            return Resp::ok(o);
        }
        return Resp::error(501, "Method not implemented");
    }

    bool authorized(const MockPveServer::Request &r) const
    {
        const QByteArray auth = r.headers.value("authorization");
        if (!auth.isEmpty())
            return auth == "PVEAPIToken=root@pam!phone=" + TokenSecret.toLatin1();
        return !currentTicket.isEmpty() && cookieTicket(r) == currentTicket;
    }

    QJsonObject issue(const QString &user)
    {
        ++ticketCounter;
        currentTicket = ticketCounter == 1 ? Ticket1 : Ticket2 + QString::number(ticketCounter);
        currentCsrf = QStringLiteral("csrf%1").arg(ticketCounter);
        return ticketData(currentTicket, currentCsrf, user);
    }
};

} // namespace

class TstCore : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_fake = FakePve();
        m_server = new MockPveServer(this);
        m_server->handler = [this](const MockPveServer::Request &r) { return m_fake.handle(r); };
        QVERIFY(m_server->isListening());
    }

    void cleanup()
    {
        delete m_server;
        m_server = nullptr;
    }

    // --- helpers --------------------------------------------------------

    void formEncodeEscapesEverything()
    {
        Pve::FormFields fields;
        fields << qMakePair(QStringLiteral("username"), QStringLiteral("root@pam"))
               << qMakePair(QStringLiteral("password"), QStringLiteral("a+b&c=d %e/å"));
        QCOMPARE(Pve::formEncode(fields),
                 QByteArray("username=root%40pam&password=a%2Bb%26c%3Dd%20%25e%2F%C3%A5"));
    }

    void hostInput_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::newRow("plain") << "pve.lan" << "pve.lan" << 8006;
        QTest::newRow("url") << "https://pve.lan:8443/" << "pve.lan" << 8443;
        QTest::newRow("host:port") << "10.0.0.5:9000" << "10.0.0.5" << 9000;
        QTest::newRow("spaces") << "  pve.lan  " << "pve.lan" << 8006;
        QTest::newRow("ipv6") << "[fe80::1]:8006" << "fe80::1" << 8006;
    }

    void hostInput()
    {
        QFETCH(QString, input);
        QFETCH(QString, host);
        QFETCH(int, port);
        ServerProfile p;
        p.setHostInput(input);
        QCOMPARE(p.host, host);
        QCOMPARE(p.port, port);
    }

    void profileIds()
    {
        ServerProfile p;
        p.username = QStringLiteral("alice");
        p.realm = QStringLiteral("pve");
        p.tokenName = QStringLiteral("phone");
        QCOMPARE(p.userId(), QStringLiteral("alice@pve"));
        QCOMPARE(p.tokenId(), QStringLiteral("alice@pve!phone"));
        p.username = QStringLiteral("bob@ldap");
        QCOMPARE(p.userId(), QStringLiteral("bob@ldap"));
    }

    // --- TfaChallenge ---------------------------------------------------

    void tfaChallengeParsesTicket()
    {
        const TfaChallenge c = TfaChallenge::parse(TfaTicket);
        QVERIFY(c.parsed);
        QVERIFY(c.totp);
        QVERIFY(c.yubico);
        QVERIFY(c.recovery);
        QVERIFY(!c.recoveryExhausted);
        QCOMPARE(c.recoveryIds, QList<int>() << 0 << 2);
        QVERIFY(!c.webauthn);
        QCOMPARE(c.methods(), QStringList() << "totp" << "yubico" << "recovery");
    }

    void tfaChallengeWebauthnOnly()
    {
        // {"webauthn":{"publicKey":{}},"recovery":[]}
        const TfaChallenge c = TfaChallenge::parse(QStringLiteral(
                "PVE:!tfa!%7B%22webauthn%22%3A%7B%22publicKey%22%3A%7B%22rpId%22%3A%22pve%22%7D%7D%2C%22recovery%22%3A%5B%5D%7D:65F00000::c2ln"));
        QVERIFY(c.parsed);
        QVERIFY(c.webauthn);
        QVERIFY(!c.recovery);
        QVERIFY(c.recoveryExhausted);
        QVERIFY(!c.hasUsableMethod());
        QVERIFY(c.methods().isEmpty());
    }

    void tfaChallengeUnknownFormatFallsBack()
    {
        const TfaChallenge c = TfaChallenge::parse(QStringLiteral("PVE:root@pam:65F00000::c2ln"));
        QVERIFY(!c.parsed);
        QCOMPARE(c.methods(), QStringList() << "totp" << "yubico" << "recovery");
    }

    void tfaCodeValidation_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QString>("code");
        QTest::addColumn<bool>("valid");
        QTest::newRow("totp") << "totp" << "123456" << true;
        QTest::newRow("totp spaced") << "totp" << "123 456" << true;
        QTest::newRow("totp 8") << "totp" << "12345678" << true;
        QTest::newRow("totp short") << "totp" << "12345" << false;
        QTest::newRow("totp letters") << "totp" << "12a456" << false;
        QTest::newRow("yubico") << "yubico" << "ccccccbcgujhingjrdejhgfnuetrgigvejhhgbkugded" << true;
        QTest::newRow("yubico upper") << "yubico" << "CCCCCCBCGUJHINGJRDEJHGFNUETRGIGVEJHHGBKUGDED" << true;
        QTest::newRow("yubico not modhex") << "yubico" << "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" << false;
        QTest::newRow("recovery") << "recovery" << "0a1b-2c3d-4e5f-6a7b" << true;
        QTest::newRow("recovery upper") << "recovery" << "0A1B-2C3D-4E5F-6A7B" << true;
        QTest::newRow("recovery short") << "recovery" << "0a1b-2c3d" << false;
        QTest::newRow("unknown") << "webauthn" << "{}" << false;
    }

    void tfaCodeValidation()
    {
        QFETCH(QString, method);
        QFETCH(QString, code);
        QFETCH(bool, valid);
        QCOMPARE(TfaChallenge::isValid(method, code), valid);
    }

    // --- ServerStore ----------------------------------------------------

    void serverStoreRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/servers.ini");
        QString id;
        {
            ServerStore store(new QSettings(path, QSettings::IniFormat));
            QVariantMap data;
            data.insert(QStringLiteral("name"), QStringLiteral("Lab"));
            data.insert(QStringLiteral("host"), QStringLiteral("https://pve.lan:8443"));
            data.insert(QStringLiteral("username"), QStringLiteral("root"));
            data.insert(QStringLiteral("realm"), QStringLiteral("pam"));
            id = store.add(data);
            QVERIFY(!id.isEmpty());
            store.setFingerprint(id, QStringLiteral("AA:BB"));
            QCOMPARE(store.count(), 1);
        }
        ServerStore store(new QSettings(path, QSettings::IniFormat));
        QCOMPARE(store.count(), 1);
        const QVariantMap p = store.get(id);
        QCOMPARE(p.value(QStringLiteral("host")).toString(), QStringLiteral("pve.lan"));
        QCOMPARE(p.value(QStringLiteral("port")).toInt(), 8443);
        QCOMPARE(p.value(QStringLiteral("fingerprint")).toString(), QStringLiteral("AA:BB"));
        QCOMPARE(p.value(QStringLiteral("userId")).toString(), QStringLiteral("root@pam"));
    }

    void serverStoreKeepsFingerprintUnlessHostChanges()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        QVariantMap data;
        data.insert(QStringLiteral("host"), QStringLiteral("pve.lan"));
        const QString id = store.add(data);
        store.setFingerprint(id, QStringLiteral("AA:BB"));

        data.insert(QStringLiteral("name"), QStringLiteral("Renamed"));
        QVERIFY(store.update(id, data));
        QCOMPARE(store.profile(id).fingerprint, QStringLiteral("AA:BB"));

        data.insert(QStringLiteral("host"), QStringLiteral("other.lan"));
        QVERIFY(store.update(id, data));
        QVERIFY(store.profile(id).fingerprint.isEmpty());
    }

    void serverStoreRejectsInvalid()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        QVERIFY(store.add(QVariantMap()).isEmpty());
        QVariantMap token;
        token.insert(QStringLiteral("host"), QStringLiteral("pve.lan"));
        token.insert(QStringLiteral("authMethod"), QStringLiteral("token"));
        QVERIFY(store.add(token).isEmpty()); // token name missing
    }

    // --- PveClient ------------------------------------------------------

    void passwordLogin()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        QSignalSpy versionSpy(&client, SIGNAL(serverVersionChanged()));

        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QCOMPARE(client.state(), PveClient::Connecting);
        QVERIFY(connectedSpy.wait());
        QCOMPARE(client.state(), PveClient::Connected);
        QCOMPARE(client.userId(), QStringLiteral("root@pam"));
        QTRY_COMPARE(client.serverVersion(), QStringLiteral("8.2.4"));

        // Authenticated POST carries the ticket cookie and the CSRF token.
        bool done = false;
        PveClient::Reply reply;
        client.post(QStringLiteral("/nodes/pve/qemu/100/status/start"),
                    [&](const PveClient::Reply &r) { reply = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QVERIFY(reply.data.toString().startsWith(QLatin1String("UPID:")));
    }

    void wrongPassword()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy failedSpy(&client, SIGNAL(loginFailed(QString,int)));

        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("nope"));
        QVERIFY(failedSpy.wait());
        QCOMPARE(failedSpy.at(0).at(1).toInt(), 401);
        QCOMPARE(client.state(), PveClient::Disconnected);
    }

    void tfaLogin()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy tfaSpy(&client, SIGNAL(tfaRequired(QString)));
        QSignalSpy tfaFailedSpy(&client, SIGNAL(tfaFailed(QString)));
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));

        client.loginWithPassword(QStringLiteral("tfa@pve"), QStringLiteral("secret"));
        QVERIFY(tfaSpy.wait());
        QCOMPARE(tfaSpy.at(0).at(0).toString(), TfaTicket);
        QCOMPARE(client.state(), PveClient::AwaitingTfa);

        // Wrong code: stay in AwaitingTfa so the user can retry.
        client.submitTfa(QStringLiteral("totp:000000"));
        QVERIFY(tfaFailedSpy.wait());
        QCOMPARE(client.state(), PveClient::AwaitingTfa);

        client.submitTfa(QStringLiteral("totp:123456"));
        QVERIFY(connectedSpy.wait());
        QCOMPARE(client.state(), PveClient::Connected);

        const QList<MockPveServer::Request> tickets = m_server->requestsTo(QStringLiteral("/api2/json/access/ticket"));
        QCOMPARE(tickets.size(), 3);
        QCOMPARE(tickets.at(2).form.value(QStringLiteral("tfa-challenge")), TfaTicket);
        QCOMPARE(tickets.at(2).form.value(QStringLiteral("username")), QStringLiteral("tfa@pve"));
    }

    void cancelDuringLoginIgnoresLateReply()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        client.logout();
        QTest::qWait(200);
        QCOMPARE(connectedSpy.count(), 0);
        QCOMPARE(client.state(), PveClient::Disconnected);
    }

    void tokenLogin()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithToken(QStringLiteral("root@pam!phone"), TokenSecret);
        QVERIFY(connectedSpy.wait());
        QCOMPARE(client.serverVersion(), QStringLiteral("8.2.4"));

        bool done = false;
        PveClient::Reply reply;
        client.post(QStringLiteral("/nodes/pve/qemu/100/status/start"),
                    [&](const PveClient::Reply &r) { reply = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(reply.ok, qPrintable(reply.error));
    }

    void badToken()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy failedSpy(&client, SIGNAL(loginFailed(QString,int)));
        client.loginWithToken(QStringLiteral("root@pam!phone"), QStringLiteral("wrong"));
        QVERIFY(failedSpy.wait());
        QCOMPARE(client.state(), PveClient::Disconnected);
    }

    void ticketRenewedBeforeRequest()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        // Renew after 100 ms; timer-driven renewal is effectively off.
        client.setTicketTimings(100, 60000);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        QSignalSpy renewedSpy(&client, SIGNAL(ticketRenewed()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());
        QTRY_VERIFY(!client.serverVersion().isEmpty());

        // Either the timer or the request triggers the renewal; the
        // request must succeed with the new ticket in both cases.
        QTest::qWait(150);
        bool done = false;
        PveClient::Reply reply;
        client.post(QStringLiteral("/nodes/pve/qemu/100/status/start"),
                    [&](const PveClient::Reply &r) { reply = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QVERIFY(renewedSpy.count() >= 1);

        const QList<MockPveServer::Request> tickets = m_server->requestsTo(QStringLiteral("/api2/json/access/ticket"));
        QVERIFY(tickets.size() >= 2);
        QCOMPARE(tickets.at(1).form.value(QStringLiteral("password")), Ticket1);
    }

    void unauthorizedExpiresSession()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        QSignalSpy expiredSpy(&client, SIGNAL(sessionExpired()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());

        m_fake.currentTicket = QStringLiteral("revoked");
        bool done = false;
        client.get(QStringLiteral("/version"), [&](const PveClient::Reply &) { done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(expiredSpy.count(), 1);
        QCOMPARE(client.state(), PveClient::Disconnected);
    }

    void callWhileDisconnectedFails()
    {
        PveClient client;
        bool done = false;
        PveClient::Reply reply;
        client.get(QStringLiteral("/version"), [&](const PveClient::Reply &r) { reply = r; done = true; });
        QVERIFY(done);
        QVERIFY(!reply.ok);
    }

    // --- SessionManager ------------------------------------------------

    void sessionManagerRemembersSecretAfterSuccess()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString id = addProfile(store, QStringLiteral("root"), QStringLiteral("pam"));

        QSignalSpy secretSpy(&manager, SIGNAL(secretRequired(QString,QString)));
        QSignalSpy failedSpy(&manager, SIGNAL(loginFailed(QString,QString)));
        QSignalSpy connectedSpy(&manager, SIGNAL(connected(QString)));

        // No stored secret yet: the UI is asked for one.
        manager.connectServer(id);
        QVERIFY(secretSpy.wait());

        // A wrong password is not stored.
        manager.login(id, QStringLiteral("nope"), true);
        QVERIFY(failedSpy.wait());
        QVERIFY(secrets.secrets.isEmpty());

        manager.login(id, QStringLiteral("secret"), true);
        QVERIFY(connectedSpy.wait());
        QTRY_COMPARE(secrets.secrets.value(store.profile(id).secretKey()), QByteArray("secret"));
        QCOMPARE(manager.connectedIds(), QStringList() << id);

        // Next time the stored secret is used without asking.
        manager.logout(id);
        QVERIFY(manager.connectedIds().isEmpty());
        secretSpy.clear();
        manager.connectServer(id);
        QVERIFY(connectedSpy.wait());
        QCOMPARE(secretSpy.count(), 0);
    }

    void sessionManagerForgetsRejectedStoredSecret()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString id = addProfile(store, QStringLiteral("root"), QStringLiteral("pam"));
        secrets.secrets.insert(store.profile(id).secretKey(), "outdated");

        QSignalSpy secretSpy(&manager, SIGNAL(secretRequired(QString,QString)));
        manager.connectServer(id);
        QVERIFY(secretSpy.wait());
        QVERIFY(!secretSpy.at(0).at(1).toString().isEmpty());
        QTRY_VERIFY(secrets.secrets.isEmpty());
    }

    void sessionManagerTfaAndNoRemember()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString id = addProfile(store, QStringLiteral("tfa"), QStringLiteral("pve"));

        QSignalSpy tfaSpy(&manager, SIGNAL(tfaRequired(QString)));
        QSignalSpy connectedSpy(&manager, SIGNAL(connected(QString)));
        manager.login(id, QStringLiteral("secret"), false);
        QVERIFY(tfaSpy.wait());
        manager.submitTfa(id, QStringLiteral("totp"), QStringLiteral(" 123456 "));
        QVERIFY(connectedSpy.wait());
        QTest::qWait(50);
        QVERIFY(secrets.secrets.isEmpty());
        QVERIFY(!store.profile(id).rememberSecret);
    }

    void sessionManagerTfaChallengeAndLastMethod()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString id = addProfile(store, QStringLiteral("tfa"), QStringLiteral("pve"));

        QSignalSpy tfaSpy(&manager, SIGNAL(tfaRequired(QString)));
        QSignalSpy connectedSpy(&manager, SIGNAL(connected(QString)));

        manager.login(id, QStringLiteral("secret"), false);
        QVERIFY(tfaSpy.wait());
        QVariantMap challenge = manager.tfaChallenge(id);
        QCOMPARE(challenge.value("methods").toStringList(),
                 QStringList() << "totp" << "yubico" << "recovery");
        QCOMPARE(challenge.value("defaultMethod").toString(), QStringLiteral("totp"));

        // Upper case and spaces are cleaned up before sending.
        manager.submitTfa(id, QStringLiteral("recovery"), QStringLiteral(" 0A1B-2C3D-4E5F-6A7B "));
        QVERIFY(connectedSpy.wait());
        QCOMPARE(store.profile(id).lastTfaMethod, QStringLiteral("recovery"));
        QVERIFY(manager.tfaChallenge(id).isEmpty());

        // Next login suggests the method used last time.
        manager.logout(id);
        manager.login(id, QStringLiteral("secret"), false);
        QVERIFY(tfaSpy.wait());
        QCOMPARE(manager.tfaChallenge(id).value("defaultMethod").toString(),
                 QStringLiteral("recovery"));
    }

    void sessionManagerSeveralSessions()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString a = addProfile(store, QStringLiteral("root"), QStringLiteral("pam"));
        const QString b = addProfile(store, QStringLiteral("root"), QStringLiteral("pam"), QStringLiteral("phone"));
        secrets.secrets.insert(store.profile(b).secretKey(), TokenSecret.toUtf8());

        QSignalSpy connectedSpy(&manager, SIGNAL(connected(QString)));
        manager.login(a, QStringLiteral("secret"), false);
        manager.connectServer(b);
        QTRY_COMPARE(connectedSpy.count(), 2);
        QCOMPARE(manager.connectedIds().size(), 2);
        QVERIFY(manager.client(a) != manager.client(b));

        manager.removeServer(a);
        QCOMPARE(manager.connectedIds(), QStringList() << b);
        QCOMPARE(store.count(), 1);
    }

    // --- Resources -----------------------------------------------------

    void resourceModelMergesUpdates()
    {
        ResourceModel model(nullptr);
        QJsonObject storage;
        storage.insert(QStringLiteral("type"), QStringLiteral("storage"));
        storage.insert(QStringLiteral("node"), QStringLiteral("pve1"));
        QJsonArray first;
        first << guest(101, QStringLiteral("db"), QStringLiteral("pve2"), QStringLiteral("lxc"), QStringLiteral("running"))
              << guest(100, QStringLiteral("web"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("running"),
                       false, QStringLiteral("prod;web os"))
              << guest(102, QStringLiteral("old"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("stopped"))
              << guest(9000, QStringLiteral("tmpl"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("stopped"), true)
              << storage;

        QSignalSpy loadedSpy(&model, SIGNAL(loadedChanged()));
        model.update(first);
        // Sorted by node, then vmid; storage is skipped.
        QCOMPARE(vmids(model), QList<int>() << 100 << 102 << 9000 << 101);
        QCOMPARE(model.runningCount(), 2);
        QCOMPARE(model.stoppedCount(), 1);   // the template is not counted
        QVERIFY(model.isLoaded());
        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(model.get(0).value(QStringLiteral("tags")).toStringList(),
                 QStringList() << QStringLiteral("prod") << QStringLiteral("web") << QStringLiteral("os"));

        // 102 starts, 101 is gone and 103 is new: rows change in place.
        QSignalSpy resetSpy(&model, SIGNAL(modelReset()));
        QSignalSpy insertSpy(&model, SIGNAL(rowsInserted(QModelIndex,int,int)));
        QSignalSpy removeSpy(&model, SIGNAL(rowsRemoved(QModelIndex,int,int)));
        QSignalSpy changeSpy(&model, SIGNAL(dataChanged(QModelIndex,QModelIndex,QVector<int>)));
        QJsonArray second;
        second << guest(100, QStringLiteral("web"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("running"),
                        false, QStringLiteral("prod;web os"))
               << guest(102, QStringLiteral("old"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("running"))
               << guest(103, QStringLiteral("new"), QStringLiteral("pve1"), QStringLiteral("lxc"), QStringLiteral("stopped"))
               << guest(9000, QStringLiteral("tmpl"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("stopped"), true);
        model.update(second);
        QCOMPARE(vmids(model), QList<int>() << 100 << 102 << 103 << 9000);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(insertSpy.count(), 1);
        QCOMPARE(removeSpy.count(), 1);
        QCOMPARE(changeSpy.count(), 1);
        QCOMPARE(model.get(1).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
        QCOMPARE(model.runningCount(), 2);
        QCOMPARE(model.stoppedCount(), 1);
    }

    void resourceModelStatusOverride()
    {
        ResourceModel model(nullptr);
        QJsonArray running;
        running << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("running"));
        model.update(running);
        QCOMPARE(model.runningCount(), 1);

        QSignalSpy changeSpy(&model, SIGNAL(dataChanged(QModelIndex,QModelIndex,QVector<int>)));
        QSignalSpy countsSpy(&model, SIGNAL(countsChanged()));
        model.overrideStatus(100, QStringLiteral("stopped"));
        QCOMPARE(changeSpy.count(), 1);
        QCOMPARE(countsSpy.count(), 1);
        QCOMPARE(model.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QCOMPARE(model.get(0).value(QStringLiteral("mem")).toDouble(), 0.0);
        QCOMPARE(model.runningCount(), 0);
        QCOMPARE(model.stoppedCount(), 1);

        // A stale /cluster/resources does not bring "running" back...
        model.update(running);
        QCOMPARE(model.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QCOMPARE(model.runningCount(), 0);

        // ...and once it agrees, the cluster is followed again.
        QJsonArray stopped;
        stopped << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("stopped"));
        model.update(stopped);
        model.update(running);
        QCOMPARE(model.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
    }

    void resourceFilterModel()
    {
        ResourceModel model(nullptr);
        QJsonArray list;
        list << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("running"),
                      false, QStringLiteral("prod"))
             << guest(101, QStringLiteral("db"), QStringLiteral("pve"), QStringLiteral("lxc"), QStringLiteral("stopped"))
             << guest(102, QStringLiteral("cache"), QStringLiteral("pve"), QStringLiteral("lxc"), QStringLiteral("running"))
             << guest(9000, QStringLiteral("tmpl"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("stopped"), true);
        model.update(list);

        ResourceFilterModel filter;
        QSignalSpy countSpy(&filter, SIGNAL(countChanged()));
        filter.setResources(&model);
        QCOMPARE(filter.count(), 4);

        filter.setShow(ResourceFilterModel::ShowRunning);
        QCOMPARE(vmids(filter), QList<int>() << 100 << 102);
        filter.setShow(ResourceFilterModel::ShowStopped);
        QCOMPARE(vmids(filter), QList<int>() << 101);
        filter.setShow(ResourceFilterModel::ShowVirtualMachines);
        QCOMPARE(vmids(filter), QList<int>() << 100 << 9000);
        filter.setShow(ResourceFilterModel::ShowContainers);
        QCOMPARE(vmids(filter), QList<int>() << 101 << 102);

        filter.setShow(ResourceFilterModel::ShowAll);
        filter.setSearchText(QStringLiteral("PROD"));
        QCOMPARE(vmids(filter), QList<int>() << 100);
        filter.setSearchText(QStringLiteral(" 10 "));
        QCOMPARE(vmids(filter), QList<int>() << 100 << 101 << 102);
        filter.setSearchText(QStringLiteral("cach"));
        QCOMPARE(vmids(filter), QList<int>() << 102);
        QCOMPARE(filter.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("cache"));
        QVERIFY(countSpy.count() > 0);
    }

    void rrdHistory()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());

        RrdHistory history;
        history.setClient(&client);
        history.setNode(QStringLiteral("pve"));
        history.setType(QStringLiteral("qemu"));
        history.setVmid(100);
        history.setTimeframe(QStringLiteral("day"));
        QTRY_VERIFY(history.isLoaded());

        // One request for all the properties, with the timeframe asked for.
        const QList<MockPveServer::Request> requests =
                m_server->requestsTo(QStringLiteral("/api2/json/nodes/pve/qemu/100/rrddata"));
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().query.value(QStringLiteral("timeframe")), QStringLiteral("day"));
        QCOMPARE(requests.first().query.value(QStringLiteral("cf")), QStringLiteral("AVERAGE"));

        QCOMPARE(history.count(), 3);
        QCOMPARE(history.timeAt(2), qint64(1120));
        QCOMPARE(history.valueAt(QStringLiteral("cpu"), 0), 0.5);
        // A sample without values is a gap, not zero.
        QVERIFY(std::isnan(history.valueAt(QStringLiteral("cpu"), 1)));
        QCOMPARE(history.latest(QStringLiteral("mem")), 200.0);
        // A series that starts late has gaps before it.
        QCOMPARE(history.values(QStringLiteral("netin")).size(), 3);
        QVERIFY(std::isnan(history.valueAt(QStringLiteral("netin"), 0)));
        QCOMPARE(history.valueAt(QStringLiteral("netin"), 2), 10.0);
        // Unknown series are all gaps.
        QVERIFY(std::isnan(history.latest(QStringLiteral("diskread"))));

        client.logout();
        QTRY_VERIFY(!history.isLoaded());
    }

    void tagColors()
    {
        // As a property string, and already parsed.
        QVariantMap colors = ResourceModel::parseTagColors(
                    QJsonValue(QStringLiteral("shape=dense,color-map=prod:FF0000:FFFFFF;web:00aa00;bad:xyz")));
        QCOMPARE(colors.size(), 2);
        QCOMPARE(colors.value(QStringLiteral("prod")).toString(), QStringLiteral("#ff0000"));
        QCOMPARE(colors.value(QStringLiteral("web")).toString(), QStringLiteral("#00aa00"));
        QJsonObject parsed;
        parsed.insert(QStringLiteral("color-map"), QStringLiteral("db:0000FF"));
        colors = ResourceModel::parseTagColors(parsed);
        QCOMPARE(colors.value(QStringLiteral("db")).toString(), QStringLiteral("#0000ff"));
        QVERIFY(ResourceModel::parseTagColors(QJsonValue()).isEmpty());

        // Fetched at login, dropped at logout.
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        ResourceModel model(&client);
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QTRY_COMPARE(model.tagColors().value(QStringLiteral("prod")).toString(), QStringLiteral("#ff0000"));
        client.logout();
        QVERIFY(model.tagColors().isEmpty());
    }

    void resourceFilterByNode()
    {
        ResourceModel model(nullptr);
        QJsonArray list;
        list << guest(100, QStringLiteral("web"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("running"))
             << guest(101, QStringLiteral("db"), QStringLiteral("pve2"), QStringLiteral("lxc"), QStringLiteral("running"));
        model.update(list);
        ResourceFilterModel filter;
        filter.setResources(&model);
        filter.setNode(QStringLiteral("pve2"));
        QCOMPARE(vmids(filter), QList<int>() << 101);
        filter.setNode(QString());
        QCOMPARE(vmids(filter), QList<int>() << 100 << 101);
    }

    void clusterOverviewMerge()
    {
        ClusterOverview overview;
        QJsonArray resources;
        resources << nodeResource(QStringLiteral("pve2"), QStringLiteral("online"), 0.5)
                  << nodeResource(QStringLiteral("pve1"), QStringLiteral("online"), 0.25)
                  << guest(100, QStringLiteral("web"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("running"))
                  << guest(101, QStringLiteral("db"), QStringLiteral("pve1"), QStringLiteral("lxc"), QStringLiteral("stopped"))
                  << guest(9000, QStringLiteral("tmpl"), QStringLiteral("pve1"), QStringLiteral("qemu"), QStringLiteral("stopped"), true);
        QJsonArray status;
        status << clusterEntry(QStringLiteral("homelab"), true)
               << nodeStatus(QStringLiteral("pve1"), true, true, QStringLiteral("10.0.0.1"))
               << nodeStatus(QStringLiteral("pve2"), true, false, QStringLiteral("10.0.0.2"))
               // pve3 is down: it is only in /cluster/status.
               << nodeStatus(QStringLiteral("pve3"), false, false, QStringLiteral("10.0.0.3"));

        QSignalSpy resetSpy(&overview, SIGNAL(modelReset()));
        overview.update(status, true, resources);
        QVERIFY(overview.isLoaded());
        QVERIFY(overview.isCluster());
        QCOMPARE(overview.clusterName(), QStringLiteral("homelab"));
        QVERIFY(overview.isQuorumKnown());
        QVERIFY(overview.isQuorate());
        QCOMPARE(nodeNames(overview), QStringList() << QStringLiteral("pve1") << QStringLiteral("pve2") << QStringLiteral("pve3"));
        QCOMPARE(overview.onlineCount(), 2);
        const QModelIndex pve1 = overview.index(0, 0);
        QCOMPARE(pve1.data(ClusterOverview::AddressRole).toString(), QStringLiteral("10.0.0.1"));
        QVERIFY(pve1.data(ClusterOverview::LocalRole).toBool());
        QCOMPARE(pve1.data(ClusterOverview::CpuRole).toDouble(), 0.25);
        QCOMPARE(pve1.data(ClusterOverview::GuestsRole).toInt(), 2);          // template not counted
        QCOMPARE(pve1.data(ClusterOverview::RunningGuestsRole).toInt(), 1);
        QVERIFY(!overview.index(2, 0).data(ClusterOverview::OnlineRole).toBool());
        QCOMPARE(resetSpy.count(), 1);

        // Same nodes again: rows change in place.
        QSignalSpy changeSpy(&overview, SIGNAL(dataChanged(QModelIndex,QModelIndex,QVector<int>)));
        resources[1] = nodeResource(QStringLiteral("pve1"), QStringLiteral("online"), 0.75);
        overview.update(status, true, resources);
        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(changeSpy.count(), 1);

        // Without permission for /cluster/status: nodes still listed.
        overview.update(QJsonArray(), false, resources);
        QVERIFY(overview.isCluster());   // more than one node
        QVERIFY(overview.clusterName().isEmpty());
        QVERIFY(!overview.isQuorumKnown());
        QCOMPARE(nodeNames(overview), QStringList() << QStringLiteral("pve1") << QStringLiteral("pve2"));

        // A server that is not in a cluster.
        QJsonArray single;
        single << nodeStatus(QStringLiteral("pve"), true, true, QStringLiteral("192.168.1.2"));
        QJsonArray singleResources;
        singleResources << nodeResource(QStringLiteral("pve"), QStringLiteral("online"), 0.1);
        overview.update(single, true, singleResources);
        QVERIFY(!overview.isCluster());
        QCOMPARE(overview.count(), 1);
    }

    void clusterOverviewFromServer()
    {
        m_fake.resources << nodeResource(QStringLiteral("pve"), QStringLiteral("online"), 0.1)
                         << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("running"));
        m_fake.clusterStatus << clusterEntry(QStringLiteral("lab"), false)
                             << nodeStatus(QStringLiteral("pve"), true, true, QStringLiteral("10.0.0.1"));

        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());

        ClusterOverview overview;
        overview.setClient(&client);
        QTRY_VERIFY(overview.isLoaded());
        QCOMPARE(overview.clusterName(), QStringLiteral("lab"));
        QVERIFY(!overview.isQuorate());
        QCOMPARE(overview.count(), 1);
        QCOMPARE(overview.index(0, 0).data(ClusterOverview::RunningGuestsRole).toInt(), 1);
        // All resources, not only guests, are asked for.
        QVERIFY(m_server->requestsTo(QStringLiteral("/api2/json/cluster/resources")).last().query.isEmpty());

        // A user without Sys.Audit still gets the nodes.
        m_fake.clusterStatusAllowed = false;
        overview.refresh();
        QTRY_VERIFY(!overview.isQuorumKnown());
        QCOMPARE(overview.count(), 1);
        QVERIFY(overview.errorString().isEmpty());

        // Logging out empties it.
        client.logout();
        QCOMPARE(overview.count(), 0);
        QVERIFY(!overview.isLoaded());
    }

    void resourceModelFollowsClient()
    {
        m_fake.resources << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("running"))
                         << guest(101, QStringLiteral("db"), QStringLiteral("pve"), QStringLiteral("lxc"), QStringLiteral("stopped"));

        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        ResourceModel model(&client);
        model.setPollInterval(50);

        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QTRY_VERIFY(model.isLoaded());
        QCOMPARE(model.count(), 2);
        const QList<MockPveServer::Request> requests = m_server->requestsTo(QStringLiteral("/api2/json/cluster/resources"));
        QVERIFY(!requests.isEmpty());
        QCOMPARE(requests.first().query.value(QStringLiteral("type")), QStringLiteral("vm"));

        // Polling picks up changes.
        m_fake.resources << guest(102, QStringLiteral("new"), QStringLiteral("pve"), QStringLiteral("lxc"), QStringLiteral("running"));
        QTRY_COMPARE(model.count(), 3);

        // Logging out empties the model and stops polling.
        client.logout();
        QCOMPARE(model.count(), 0);
        QVERIFY(!model.isLoaded());
        QCOMPARE(model.runningCount(), 0);
        // A poll already on the wire may still arrive; its reply is ignored.
        QTest::qWait(100);
        QCOMPARE(model.count(), 0);
        const int polled = m_server->requestsTo(QStringLiteral("/api2/json/cluster/resources")).size();
        QTest::qWait(200);
        QCOMPARE(m_server->requestsTo(QStringLiteral("/api2/json/cluster/resources")).size(), polled);
    }

    void sessionManagerCountsGuests()
    {
        m_fake.resources << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("running"))
                         << guest(101, QStringLiteral("db"), QStringLiteral("pve"), QStringLiteral("lxc"), QStringLiteral("stopped"))
                         << guest(9000, QStringLiteral("tmpl"), QStringLiteral("pve"), QStringLiteral("qemu"), QStringLiteral("stopped"), true);

        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString id = addProfile(store, QStringLiteral("root"), QStringLiteral("pam"));

        QSignalSpy countsSpy(&manager, SIGNAL(countsChanged()));
        manager.login(id, QStringLiteral("secret"), false);
        QTRY_COMPARE(manager.runningCount(), 1);
        QCOMPARE(manager.stoppedCount(), 1);
        QCOMPARE(manager.resources(id)->count(), 3);
        QVERIFY(countsSpy.count() > 0);

        manager.logout(id);
        QCOMPARE(manager.runningCount(), 0);
        QCOMPARE(manager.stoppedCount(), 0);
        QVERIFY(manager.resources(QStringLiteral("no-such-profile")) == nullptr);
    }

    void guestDetailsStatusAndAddresses()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());

        GuestDetails details;
        details.setClient(&client);
        details.setNode(QStringLiteral("pve"));
        details.setType(QStringLiteral("qemu"));
        details.setVmid(100);
        QTRY_VERIFY(details.isLoaded());
        QCOMPARE(details.status().value(QStringLiteral("name")).toString(), QStringLiteral("web"));
        // Loopback and link-local addresses are left out.
        QTRY_COMPARE(details.addresses(),
                     QStringList() << QStringLiteral("192.168.1.10") << QStringLiteral("fd00::10"));
        // Properties set one after another cause a single status request.
        QCOMPARE(m_server->requestsTo(QStringLiteral("/api2/json/nodes/pve/qemu/100/status/current")).size(), 1);
    }

    void lxcAddressParsing()
    {
        const QByteArray json =
                "[{\"name\": \"lo\", \"inet\": \"127.0.0.1/8\", \"inet6\": \"::1/128\"},"
                " {\"name\": \"eth0\", \"inet\": \"10.0.0.5/24\", \"inet6\": \"fe80::1/64\"},"
                " {\"name\": \"eth1\", \"inet6\": \"fd00::5/64\"}]";
        QCOMPARE(GuestDetails::parseLxcAddresses(QJsonDocument::fromJson(json).array()),
                 QStringList() << QStringLiteral("10.0.0.5") << QStringLiteral("fd00::5"));
    }

    // --- Power actions -------------------------------------------------

    void availableActions()
    {
        PowerActions actions(nullptr);
        const QString none;
        QCOMPARE(actions.availableActions(QStringLiteral("qemu"), QStringLiteral("stopped"), none, false, none),
                 QStringList() << QStringLiteral("start"));
        QCOMPARE(actions.availableActions(QStringLiteral("qemu"), QStringLiteral("running"), QStringLiteral("running"), false, none),
                 QStringList() << QStringLiteral("shutdown") << QStringLiteral("reboot") << QStringLiteral("suspend")
                               << QStringLiteral("stop") << QStringLiteral("reset"));
        QCOMPARE(actions.availableActions(QStringLiteral("lxc"), QStringLiteral("running"), none, false, none),
                 QStringList() << QStringLiteral("shutdown") << QStringLiteral("reboot") << QStringLiteral("stop"));
        QCOMPARE(actions.availableActions(QStringLiteral("qemu"), QStringLiteral("running"), QStringLiteral("paused"), false, none),
                 QStringList() << QStringLiteral("resume") << QStringLiteral("stop"));
        // Templates and locked guests get nothing, except a hibernated VM.
        QVERIFY(actions.availableActions(QStringLiteral("qemu"), QStringLiteral("stopped"), none, true, none).isEmpty());
        QVERIFY(actions.availableActions(QStringLiteral("qemu"), QStringLiteral("running"), none, false,
                                         QStringLiteral("backup")).isEmpty());
        QCOMPARE(actions.availableActions(QStringLiteral("qemu"), QStringLiteral("stopped"), none, false,
                                          QStringLiteral("suspended")),
                 QStringList() << QStringLiteral("start"));
    }

    void taskWatcherNode()
    {
        QCOMPARE(TaskWatcher::nodeOf(QStringLiteral("UPID:pve1:0000ABCD:00001234:65F00000:qmstart:100:root@pam:")),
                 QStringLiteral("pve1"));
        QVERIFY(TaskWatcher::nodeOf(QStringLiteral("garbage")).isEmpty());
    }

    void powerActionsRunAndReport()
    {
        QTemporaryDir dir;
        ServerStore store(new QSettings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat));
        MemorySecretBackend secrets;
        SessionManager manager(&store, &secrets);
        manager.setTlsEnabled(false);
        const QString id = addProfile(store, QStringLiteral("root"), QStringLiteral("pam"));
        QSignalSpy connectedSpy(&manager, SIGNAL(connected(QString)));
        manager.login(id, QStringLiteral("secret"), false);
        QVERIFY(connectedSpy.wait());

        PowerActions actions(&manager);
        actions.setPollInterval(20);
        // The cluster keeps saying "running" while the guest has stopped.
        m_fake.resources << guest(100, QStringLiteral("web"), QStringLiteral("pve"), QStringLiteral("qemu"),
                                  QStringLiteral("running"));
        m_fake.guestStatus = QStringLiteral("stopped");
        manager.setPollInterval(30);
        ResourceModel *resources = manager.resources(id);
        resources->refresh();
        QTRY_COMPARE(resources->runningCount(), 1);
        QSignalSpy finishedSpy(&actions, SIGNAL(finished(QString,int,QString,QString,bool,QString)));

        // Not sent: unknown action for the type, or nobody logged in.
        QVERIFY(!actions.run(id, QStringLiteral("pve"), QStringLiteral("lxc"), 100, QStringLiteral("reset"), QStringLiteral("web")));
        QVERIFY(!actions.run(QStringLiteral("nope"), QStringLiteral("pve"), QStringLiteral("qemu"), 100,
                             QStringLiteral("stop"), QStringLiteral("web")));

        QVERIFY(actions.run(id, QStringLiteral("pve"), QStringLiteral("qemu"), 100, QStringLiteral("stop"), QStringLiteral("web")));
        QCOMPARE(actions.busyGuests(), QStringList() << PowerActions::guestKey(id, 100));
        // One action per guest at a time.
        QVERIFY(!actions.run(id, QStringLiteral("pve"), QStringLiteral("qemu"), 100, QStringLiteral("stop"), QStringLiteral("web")));

        QTRY_COMPARE(finishedSpy.count(), 1);
        QList<QVariant> args = finishedSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), id);
        QCOMPARE(args.at(1).toInt(), 100);
        QCOMPARE(args.at(2).toString(), QStringLiteral("stop"));
        QCOMPARE(args.at(3).toString(), QStringLiteral("web"));
        QCOMPARE(args.at(4).toBool(), true);
        QVERIFY(actions.busyGuests().isEmpty());
        // The POST carried the CSRF token, and the task was polled until it stopped.
        const QList<MockPveServer::Request> posts = m_server->requestsTo(QStringLiteral("/api2/json/nodes/pve/qemu/100/status/stop"));
        QCOMPARE(posts.size(), 1);
        QCOMPARE(QString::fromLatin1(posts.first().headers.value("csrfpreventiontoken")), m_fake.currentCsrf);
        QCOMPARE(m_fake.taskPolls.size(), 1);
        QCOMPARE(m_fake.taskPolls.constBegin().value(), 2);
        // The list shows the real status, and stale polls don't undo it.
        QTRY_COMPARE(resources->get(0).value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QTest::qWait(150);
        QCOMPARE(resources->get(0).value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QCOMPARE(resources->runningCount(), 0);

        // A task that ends with an error is reported with its exit status.
        QVERIFY(actions.run(id, QStringLiteral("pve"), QStringLiteral("qemu"), 100, QStringLiteral("reset"), QStringLiteral("web")));
        QTRY_COMPARE(finishedSpy.count(), 1);
        args = finishedSpy.takeFirst();
        QCOMPARE(args.at(4).toBool(), false);
        QCOMPARE(args.at(5).toString(), QStringLiteral("VM is locked (backup)"));

        // Logging out while an action is under way still reports it once
        // and clears the busy state.
        QVERIFY(actions.run(id, QStringLiteral("pve"), QStringLiteral("qemu"), 100, QStringLiteral("stop"), QStringLiteral("web")));
        manager.logout(id);
        QTRY_COMPARE(finishedSpy.count(), 1);
        QVERIFY(actions.busyGuests().isEmpty());
    }

    // --- Console ---------------------------------------------------------

    void consoleProtocolFraming()
    {
        QCOMPARE(ConsoleProtocol::login(QStringLiteral("root@pam"), QStringLiteral("PVEVNC:x")),
                 QByteArray("root@pam:PVEVNC:x\n"));
        // The length counts bytes, not characters.
        QCOMPARE(ConsoleProtocol::input(QString::fromUtf8("l\xc3\xa5s").toUtf8()),
                 QByteArray("0:4:l\xc3\xa5s"));
        QCOMPARE(ConsoleProtocol::resize(80, 24), QByteArray("1:80:24:"));
        QCOMPARE(ConsoleProtocol::ping(), QByteArray("2"));
    }

    void webSocketFraming()
    {
        // RFC 6455, section 1.3.
        QCOMPARE(WebSocketClient::acceptKey("dGhlIHNhbXBsZSBub25jZQ=="),
                 QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
        QCOMPARE(WebSocketClient::frame(0x1, "hi", false), QByteArray("\x81\x02hi"));
        const QByteArray medium(300, 'x');
        QCOMPARE(WebSocketClient::frame(0x2, medium, false).left(4), QByteArray("\x82\x7e\x01\x2c"));
        const QByteArray large(70000, 'x');
        QCOMPARE(WebSocketClient::frame(0x2, large, false).left(10),
                 QByteArray("\x82\x7f\x00\x00\x00\x00\x00\x01\x11\x70", 10));
        // Client frames are masked: same length, mask bit set, payload hidden.
        const QByteArray masked = WebSocketClient::frame(0x1, "hello", true);
        QCOMPARE(masked.size(), 2 + 4 + 5);
        QCOMPARE(static_cast<uchar>(masked.at(1)), uchar(0x80 | 5));
    }

    void consoleSessionEndToEnd()
    {
        MockWebSocket *peer = nullptr;
        MockPveServer::Request upgrade;
        m_server->upgradeHandler = [&](const MockPveServer::Request &r, QTcpSocket *socket) {
            upgrade = r;
            peer = new MockWebSocket(socket, r.headers.value("sec-websocket-key"),
                                     r.headers.value("sec-websocket-protocol"), this);
            peer->onMessage = [&peer](const QByteArray &m) {
                if (m == "root@pam:PVEVNC:abc+/=:sig\n")
                    peer->sendBinary("OKwelcome\r\n");
            };
        };

        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);
        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());

        PveConsoleSession session;
        session.setPingInterval(50);
        session.setClient(&client);
        session.setNode(QStringLiteral("pve"));
        session.setType(QStringLiteral("lxc"));
        session.setVmid(200);
        QByteArray received;
        connect(&session, &TerminalBackend::dataReceived, [&](const QByteArray &d) { received += d; });

        session.resize(100, 30);
        session.open();
        QCOMPARE(session.state(), TerminalBackend::Connecting);
        session.write("typed early");
        QTRY_COMPARE(session.state(), TerminalBackend::Connected);
        QTRY_COMPARE(received, QByteArray("welcome\r\n"));

        // The WebSocket carried the ticket cookie and the console ticket.
        QCOMPARE(upgrade.path, QStringLiteral("/api2/json/nodes/pve/lxc/200/vncwebsocket"));
        QCOMPARE(upgrade.query.value(QStringLiteral("port")), QStringLiteral("5900"));
        QCOMPARE(upgrade.query.value(QStringLiteral("vncticket")), QStringLiteral("PVEVNC:abc+/=:sig"));
        QCOMPARE(cookieTicket(upgrade), m_fake.currentTicket);
        QCOMPARE(upgrade.headers.value("sec-websocket-protocol"), QByteArray("binary"));

        // After "OK": the size, then what was typed while connecting.
        QTRY_VERIFY(peer->messages.size() >= 3);
        QCOMPARE(peer->messages.at(1), QByteArray("1:100:30:"));
        QCOMPARE(peer->messages.at(2), QByteArray("0:11:typed early"));

        session.write("ls\r");
        session.resize(100, 30);   // unchanged: not sent again
        session.resize(80, 24);
        QTRY_VERIFY(peer->messages.contains("0:3:ls\r"));
        QTRY_VERIFY(peer->messages.contains("1:80:24:"));
        QCOMPARE(peer->messages.count("1:100:30:"), 1);
        QTRY_VERIFY(peer->messages.contains("2"));
        QVERIFY(peer->allMasked);

        // Pings from the server are answered; a large message arrives whole.
        peer->sendPing("p");
        const QByteArray big(1000, 'z');
        peer->sendBinary(big);
        QTRY_VERIFY(received.endsWith(big));
        QTRY_COMPARE(peer->pongs, QList<QByteArray>() << QByteArray("p"));

        // The server ending the console closes the session without an error.
        peer->sendClose();
        QTRY_COMPARE(session.state(), TerminalBackend::Closed);
        QVERIFY(session.errorString().isEmpty());
    }

    void consoleSessionErrors()
    {
        PveClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->serverPort(), false);

        PveConsoleSession session;
        session.setClient(&client);
        session.setNode(QStringLiteral("pve"));
        session.setType(QStringLiteral("lxc"));
        session.setVmid(201);
        session.open();
        QCOMPARE(session.state(), TerminalBackend::Closed);   // not logged in
        QVERIFY(!session.errorString().isEmpty());

        QSignalSpy connectedSpy(&client, SIGNAL(connected()));
        client.loginWithPassword(QStringLiteral("root@pam"), QStringLiteral("secret"));
        QVERIFY(connectedSpy.wait());
        session.open();   // no termproxy for 201
        QTRY_COMPARE(session.state(), TerminalBackend::Closed);
        QVERIFY(!session.errorString().isEmpty());

        // The WebSocket is refused.
        session.setVmid(200);
        m_server->upgradeHandler = [](const MockPveServer::Request &, QTcpSocket *socket) {
            socket->write("HTTP/1.1 401 No ticket\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
        };
        session.open();
        QTRY_COMPARE(session.state(), TerminalBackend::Closed);
        QCOMPARE(session.errorString(), QStringLiteral("Permission denied"));
    }

    void terminalScreenText()
    {
        TerminalScreen screen(5, 20);
        QSignalSpy damagedSpy(&screen, SIGNAL(damaged()));
        screen.feed("hello\r\nworld");
        QCOMPARE(damagedSpy.count(), 1);
        QCOMPARE(screen.lineText(0), QStringLiteral("hello"));
        QCOMPARE(screen.lineText(1), QStringLiteral("world"));
        QCOMPARE(screen.cursorRow(), 1);
        QCOMPARE(screen.cursorColumn(), 5);

        // Colours and attributes.
        screen.feed("\r\n\x1b[1;31mR\x1b[0m\x1b[7mV\x1b[0mn");
        const TerminalScreen::Cell red = screen.cell(2, 0);
        QCOMPARE(red.text, QStringLiteral("R"));
        QVERIFY(red.bold);
        QVERIFY(red.foreground != screen.defaultForeground());
        QVERIFY(red.defaultBackground);
        const TerminalScreen::Cell reversed = screen.cell(2, 1);
        QCOMPARE(reversed.background, screen.defaultForeground());
        QCOMPARE(reversed.foreground, screen.defaultBackground());
        QCOMPARE(screen.cell(2, 2).foreground, screen.defaultForeground());

        // Wide characters take two cells.
        screen.feed(QString::fromUtf8("\r\n\xe6\x97\xa5x").toUtf8());
        QCOMPARE(screen.cell(3, 0).width, 2);
        QCOMPARE(screen.cell(3, 1).width, 0);
        QCOMPARE(screen.lineText(3), QString::fromUtf8("\xe6\x97\xa5x"));

        // Hiding the cursor.
        screen.feed("\x1b[?25l");
        QVERIFY(!screen.isCursorVisible());

        // vim switches to the alternate screen and back.
        QVERIFY(!screen.isAltScreen());
        screen.feed("\x1b[?1049h");
        QVERIFY(screen.isAltScreen());
        screen.feed("\x1b[?1049l");
        QVERIFY(!screen.isAltScreen());
    }

    void terminalScreenScrollback()
    {
        TerminalScreen screen(3, 10);
        screen.feed("1\r\n2\r\n3\r\n4\r\n5");
        QCOMPARE(screen.scrollbackSize(), 2);
        QCOMPARE(screen.lineText(-2), QStringLiteral("1"));
        QCOMPARE(screen.lineText(-1), QStringLiteral("2"));
        QCOMPARE(screen.lineText(0), QStringLiteral("3"));
        QCOMPARE(screen.lineText(2), QStringLiteral("5"));

        // Growing the screen pulls lines back from the scrollback.
        screen.setSize(4, 10);
        QCOMPARE(screen.scrollbackSize(), 1);
        QCOMPARE(screen.lineText(0), QStringLiteral("2"));

        screen.setScrollbackLimit(0);
        QCOMPARE(screen.scrollbackSize(), 0);
        QCOMPARE(screen.lineText(-1), QString());
    }

    void terminalScreenKeyboard()
    {
        TerminalScreen screen(5, 20);
        QByteArray out;
        connect(&screen, &TerminalScreen::output, [&](const QByteArray &d) { out += d; });

        screen.sendText(QStringLiteral("a\xe5"));
        QCOMPARE(out, QString::fromUtf8("a\xc3\xa5").toUtf8());
        out.clear();
        screen.sendKey(TerminalScreen::KeyEnter);
        QCOMPARE(out, QByteArray("\r"));
        out.clear();
        screen.sendText(QStringLiteral("c"), TerminalScreen::CtrlModifier);
        QCOMPARE(out, QByteArray("\x03"));
        out.clear();
        screen.sendText(QStringLiteral("C"), TerminalScreen::CtrlModifier);
        QCOMPARE(out, QByteArray("\x03"));
        out.clear();
        screen.sendText(QStringLiteral("x"), TerminalScreen::AltModifier);
        QCOMPARE(out, QByteArray("\x1bx"));
        out.clear();
        screen.sendKey(TerminalScreen::KeyUp);
        QCOMPARE(out, QByteArray("\x1b[A"));
        out.clear();

        // vim and htop switch the arrows to application mode.
        screen.feed("\x1b[?1h");
        screen.sendKey(TerminalScreen::KeyUp);
        QCOMPARE(out, QByteArray("\x1bOA"));
        out.clear();

        // Pasted text: newlines become Enter, bracketed when asked for.
        screen.paste(QStringLiteral("a\nb"));
        QCOMPARE(out, QByteArray("a\rb"));
        out.clear();
        screen.feed("\x1b[?2004h");
        screen.paste(QStringLiteral("x"));
        QCOMPARE(out, QByteArray("\x1b[200~x\x1b[201~"));
        out.clear();

        // Answers to terminal queries go out too (device attributes).
        screen.feed("\x1b[c");
        QVERIFY(out.startsWith("\x1b[?"));
    }

private:
    QString addProfile(ServerStore &store, const QString &user, const QString &realm,
                       const QString &tokenName = QString())
    {
        QVariantMap data;
        data.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
        data.insert(QStringLiteral("port"), m_server->serverPort());
        data.insert(QStringLiteral("username"), user);
        data.insert(QStringLiteral("realm"), realm);
        if (!tokenName.isEmpty()) {
            data.insert(QStringLiteral("authMethod"), QStringLiteral("token"));
            data.insert(QStringLiteral("tokenName"), tokenName);
        }
        return store.add(data);
    }

    MockPveServer *m_server = nullptr;
    FakePve m_fake;
};

QTEST_GUILESS_MAIN(TstCore)
#include "tst_core.moc"
