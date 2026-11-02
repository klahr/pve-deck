// SPDX-License-Identifier: GPL-3.0-or-later
#include "tfachallenge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace {

const QString TfaPrefix = QStringLiteral("!tfa!");

// A key counts as present unless it is missing, null, false, or empty.
bool present(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Array: return !v.toArray().isEmpty();
    case QJsonValue::Object: return !v.toObject().isEmpty();
    case QJsonValue::String: return !v.toString().isEmpty();
    case QJsonValue::Double: return v.toDouble() != 0;
    default: return false;
    }
}

} // namespace

TfaChallenge TfaChallenge::parse(const QString &ticket)
{
    TfaChallenge c;

    // Fallback when the format is not recognised: offer everything the
    // app supports and let the server decide.
    TfaChallenge fallback;
    fallback.totp = true;
    fallback.yubico = true;
    fallback.recovery = true;

    const QStringList parts = ticket.split(QLatin1Char(':'));
    if (parts.size() < 2 || !parts.at(1).startsWith(TfaPrefix))
        return fallback;

    const QString encoded = parts.at(1).mid(TfaPrefix.size());
    const QByteArray json = QUrl::fromPercentEncoding(encoded.toLatin1()).toUtf8();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return fallback;

    const QJsonObject o = doc.object();
    c.parsed = true;
    c.totp = present(o.value(QStringLiteral("totp")));
    c.yubico = present(o.value(QStringLiteral("yubico")));
    c.webauthn = present(o.value(QStringLiteral("webauthn")));
    c.u2f = present(o.value(QStringLiteral("u2f")));

    // An empty list means recovery keys exist but all have been used.
    const QJsonValue recovery = o.value(QStringLiteral("recovery"));
    if (recovery.isArray()) {
        foreach (const QJsonValue &id, recovery.toArray())
            c.recoveryIds.append(id.toInt());
        c.recovery = !c.recoveryIds.isEmpty();
        c.recoveryExhausted = c.recoveryIds.isEmpty();
    } else {
        c.recovery = present(recovery);
    }
    return c;
}

QStringList TfaChallenge::methods() const
{
    QStringList out;
    if (totp)
        out << QStringLiteral("totp");
    if (yubico)
        out << QStringLiteral("yubico");
    if (recovery)
        out << QStringLiteral("recovery");
    return out;
}

bool TfaChallenge::hasUsableMethod() const
{
    return totp || yubico || recovery;
}

QVariantMap TfaChallenge::toVariantMap() const
{
    QVariantList ids;
    foreach (int id, recoveryIds)
        ids.append(id);

    QVariantMap map;
    map.insert(QStringLiteral("parsed"), parsed);
    map.insert(QStringLiteral("methods"), methods());
    map.insert(QStringLiteral("recoveryIds"), ids);
    map.insert(QStringLiteral("recoveryExhausted"), recoveryExhausted);
    map.insert(QStringLiteral("webauthn"), webauthn || u2f);
    map.insert(QStringLiteral("usable"), hasUsableMethod());
    return map;
}

QString TfaChallenge::normalize(const QString &method, const QString &value)
{
    QString v = value;
    v.remove(QRegularExpression(QStringLiteral("\\s")));
    if (method == QLatin1String("totp"))
        v.remove(QLatin1Char('-'));
    else
        v = v.toLower();
    return v;
}

bool TfaChallenge::isValid(const QString &method, const QString &value)
{
    const QString v = normalize(method, value);
    if (method == QLatin1String("totp")) {
        static const QRegularExpression re(QStringLiteral("^[0-9]{6,8}$"));
        return re.match(v).hasMatch();
    }
    if (method == QLatin1String("yubico")) {
        // Modhex; 44 characters for a standard OTP (12 id + 32 token).
        static const QRegularExpression re(QStringLiteral("^[cbdefghijklnrtuv]{32,64}$"));
        return re.match(v).hasMatch();
    }
    if (method == QLatin1String("recovery")) {
        // Same format check as the Proxmox web UI.
        static const QRegularExpression re(QStringLiteral("^[0-9a-f]{4}(-[0-9a-f]{4}){3}$"));
        return re.match(v).hasMatch();
    }
    return false;
}
