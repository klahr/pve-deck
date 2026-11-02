// SPDX-License-Identifier: GPL-3.0-or-later
#include "consoleprotocol.h"

namespace ConsoleProtocol {

QByteArray login(const QString &user, const QString &ticket)
{
    return user.toUtf8() + ':' + ticket.toUtf8() + '\n';
}

QByteArray input(const QByteArray &data)
{
    return "0:" + QByteArray::number(data.size()) + ':' + data;
}

QByteArray resize(int columns, int rows)
{
    return "1:" + QByteArray::number(columns) + ':' + QByteArray::number(rows) + ':';
}

QByteArray ping()
{
    return QByteArrayLiteral("2");
}

} // namespace ConsoleProtocol
