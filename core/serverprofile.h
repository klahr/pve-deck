// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_SERVERPROFILE_H
#define PVEDECK_SERVERPROFILE_H

#include <QString>
#include <QVariantMap>

// Connection settings for one Proxmox VE server or cluster.
// Never holds secrets; those live in a SecretBackend.
struct ServerProfile
{
    enum AuthMethod { PasswordAuth, TokenAuth };

    QString id;
    QString name;
    QString host;
    int port = 8006;
    QString username = QStringLiteral("root");
    QString realm = QStringLiteral("pam");
    AuthMethod authMethod = PasswordAuth;
    QString tokenName;
    QString fingerprint;          // pinned certificate, "AB:CD:..."
    bool rememberSecret = true;
    QString lastTfaMethod;        // "totp", "yubico" or "recovery"

    // user@realm
    QString userId() const;
    // user@realm!tokenname
    QString tokenId() const;
    QString displayName() const;
    bool isValid() const;

    // Accepts "https://pve.lan:8006/", "pve.lan:8443", "[fe80::1]:8006", ...
    void setHostInput(const QString &input);

    // Key used in the secret store.
    QString secretKey() const;

    QVariantMap toVariantMap() const;
    static ServerProfile fromVariantMap(const QVariantMap &map);

    static QString authMethodToString(AuthMethod method);
    static AuthMethod authMethodFromString(const QString &value);
};

#endif // PVEDECK_SERVERPROFILE_H
