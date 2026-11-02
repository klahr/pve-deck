// SPDX-License-Identifier: GPL-3.0-or-later
#include "formencoding.h"

#include <QCryptographicHash>
#include <QStringList>
#include <QUrl>

namespace Pve {

QByteArray formEncode(const FormFields &fields)
{
    QByteArray out;
    for (int i = 0; i < fields.size(); ++i) {
        if (i > 0)
            out.append('&');
        out.append(QUrl::toPercentEncoding(fields.at(i).first));
        out.append('=');
        out.append(QUrl::toPercentEncoding(fields.at(i).second));
    }
    return out;
}

QString certificateFingerprint(const QSslCertificate &certificate)
{
    if (certificate.isNull())
        return QString();
    const QByteArray hex = certificate.digest(QCryptographicHash::Sha256).toHex().toUpper();
    QStringList parts;
    for (int i = 0; i + 1 < hex.size(); i += 2)
        parts.append(QString::fromLatin1(hex.mid(i, 2)));
    return parts.join(QLatin1Char(':'));
}

} // namespace Pve
