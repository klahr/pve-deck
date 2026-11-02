// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_CONSOLEPROTOCOL_H
#define PVEDECK_CONSOLEPROTOCOL_H

#include <QByteArray>
#include <QString>

// Framing of the Proxmox termproxy WebSocket, as used by its xterm.js client.
// Client -> server:
//   "<user>:<ticket>\n"      once, right after the WebSocket opens
//   "0:<len>:<data>"         input; len is the byte length of data
//   "1:<cols>:<rows>:"       terminal size
//   "2"                      keep-alive, about every 30 s
// Server -> client: "OK" once, then raw terminal output.
namespace ConsoleProtocol {

QByteArray login(const QString &user, const QString &ticket);
QByteArray input(const QByteArray &data);
QByteArray resize(int columns, int rows);
QByteArray ping();

} // namespace ConsoleProtocol

#endif // PVEDECK_CONSOLEPROTOCOL_H
