// SPDX-License-Identifier: GPL-3.0-or-later
#include "appinfo.h"

AppInfo::AppInfo(const QString &version, QObject *parent)
    : QObject(parent)
    , m_version(version.isEmpty() ? QStringLiteral("0.0.0") : version)
{
}

QString AppInfo::version() const
{
    return m_version;
}

QString AppInfo::name() const
{
    return QStringLiteral("PVE Deck");
}
