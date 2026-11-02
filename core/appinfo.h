// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_APPINFO_H
#define PVEDECK_APPINFO_H

#include <QObject>
#include <QString>

class AppInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit AppInfo(const QString &version, QObject *parent = nullptr);

    QString version() const;
    QString name() const;

private:
    QString m_version;
};

#endif // PVEDECK_APPINFO_H
