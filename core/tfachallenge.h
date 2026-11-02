// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_TFACHALLENGE_H
#define PVEDECK_TFACHALLENGE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Second-factor challenge from a partial Proxmox VE (7+) ticket.
//
// When a user has 2FA, POST /access/ticket returns NeedTFA=1 and a
// ticket of the form
//     PVE:!tfa!<url-encoded JSON>:<timestamp>::<signature>
// where the JSON lists the methods the user may use, e.g.
//     {"totp":true,"recovery":[0,2,5],"webauthn":{...},"yubico":true}
// The response is sent back as "<method>:<value>".
struct TfaChallenge
{
    bool parsed = false;      // false: unknown format, all methods offered
    bool totp = false;
    bool yubico = false;
    bool recovery = false;    // at least one unused recovery key
    bool recoveryExhausted = false; // recovery keys configured, all used
    QList<int> recoveryIds;   // IDs of unused recovery keys, if known
    bool webauthn = false;    // not usable from the app
    bool u2f = false;         // not usable from the app

    static TfaChallenge parse(const QString &ticket);

    // Methods the app can answer, most convenient first:
    // "totp", "yubico", "recovery".
    QStringList methods() const;
    bool hasUsableMethod() const;

    QVariantMap toVariantMap() const;

    // Cleans up user input: removes spaces; TOTP also dashes;
    // Yubico and recovery keys are lower-cased.
    static QString normalize(const QString &method, const QString &value);
    static bool isValid(const QString &method, const QString &value);
};

#endif // PVEDECK_TFACHALLENGE_H
