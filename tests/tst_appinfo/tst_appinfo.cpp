// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "appinfo.h"

class TstAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void version()
    {
        AppInfo info(QStringLiteral("1.2.3"));
        QCOMPARE(info.version(), QStringLiteral("1.2.3"));
    }

    void emptyVersionFallsBack()
    {
        AppInfo info((QString()));
        QCOMPARE(info.version(), QStringLiteral("0.0.0"));
    }
};

QTEST_GUILESS_MAIN(TstAppInfo)
#include "tst_appinfo.moc"
