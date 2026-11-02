// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_TERMINALBACKEND_H
#define PVEDECK_TERMINALBACKEND_H

#include <QByteArray>
#include <QObject>
#include <QString>

// Where a terminal's bytes come from and go to: the Proxmox console
// (PveConsoleSession) or, later, SSH. TerminalItem talks only to this.
class TerminalBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)

public:
    enum State {
        Idle,
        Connecting,
        Connected,
        Closed      // ended normally, or failed if errorString is set
    };
    Q_ENUM(State)

    explicit TerminalBackend(QObject *parent = nullptr) : QObject(parent) { }

    State state() const { return m_state; }
    QString errorString() const { return m_error; }

    Q_INVOKABLE virtual void open() = 0;
    Q_INVOKABLE virtual void close() = 0;
    // Keyboard input, already encoded for the terminal.
    virtual void write(const QByteArray &data) = 0;
    virtual void resize(int columns, int rows) = 0;

signals:
    void stateChanged();
    void dataReceived(const QByteArray &data);

protected:
    void setState(State state, const QString &error = QString())
    {
        if (state == m_state && error == m_error)
            return;
        m_state = state;
        m_error = error;
        emit stateChanged();
    }

private:
    State m_state = Idle;
    QString m_error;
};

#endif // PVEDECK_TERMINALBACKEND_H
