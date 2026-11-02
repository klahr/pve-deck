// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "formencoding.h"
#include "memorysecretbackend.h"
#include "mockpveserver.h"
#include "pveclient.h"
#include "serverprofile.h"
#include "serverstore.h"
#include "sessionmanager.h"
#include "tfachallenge.h"

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
        const QString path = dir.filePath(QStringLiteral("servers.ini"));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
        ServerStore store(new QSettings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat));
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
