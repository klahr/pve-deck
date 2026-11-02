// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_FORMENCODING_H
#define PVEDECK_FORMENCODING_H

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QSslCertificate>

namespace Pve {

typedef QList<QPair<QString, QString> > FormFields;

// application/x-www-form-urlencoded body. Unlike QUrlQuery this also
// encodes '+', '&', '=' and so on, so passwords survive unchanged.
QByteArray formEncode(const FormFields &fields);

// SHA-256 fingerprint as "AB:CD:...", the same format the Proxmox
// web UI shows under Node > System > Certificates.
QString certificateFingerprint(const QSslCertificate &certificate);

} // namespace Pve

#endif // PVEDECK_FORMENCODING_H
