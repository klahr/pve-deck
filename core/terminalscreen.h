// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_TERMINALSCREEN_H
#define PVEDECK_TERMINALSCREEN_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include <vterm.h>

// The terminal emulator: a libvterm screen plus scrollback, without any
// drawing. Terminal output goes in with feed(); keys are encoded with
// sendKey()/sendText() and come out of output(), ready for the backend.
//
// Rows are numbered from the top of the screen, 0..rows()-1. Scrollback
// lines have negative numbers: -1 is the line just above the screen.
class TerminalScreen : public QObject
{
    Q_OBJECT

public:
    // 0xAARRGGBB, the same layout as QRgb (core does not link QtGui).
    typedef quint32 Rgb;

    struct Cell {
        QString text;       // empty for a blank cell
        Rgb foreground = 0;
        Rgb background = 0;
        int width = 1;      // 2 for wide characters, 0 for their second half
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        bool defaultBackground = true;
    };

    enum Key {
        KeyEnter, KeyTab, KeyBackspace, KeyEscape,
        KeyUp, KeyDown, KeyLeft, KeyRight,
        KeyInsert, KeyDelete, KeyHome, KeyEnd, KeyPageUp, KeyPageDown,
        KeyF1, KeyF2, KeyF3, KeyF4, KeyF5, KeyF6,
        KeyF7, KeyF8, KeyF9, KeyF10, KeyF11, KeyF12
    };

    // Same values as libvterm's VTermModifier.
    enum Modifier {
        NoModifier = 0,
        ShiftModifier = 1,
        AltModifier = 2,
        CtrlModifier = 4
    };

    explicit TerminalScreen(int rows = 24, int columns = 80, QObject *parent = nullptr);
    ~TerminalScreen();

    int rows() const;
    int columns() const;
    void setSize(int rows, int columns);

    void setDefaultColors(Rgb foreground, Rgb background);
    Rgb defaultForeground() const;
    Rgb defaultBackground() const;

    void setScrollbackLimit(int lines);
    int scrollbackSize() const;

    // Terminal output from the backend.
    void feed(const QByteArray &data);

    void sendKey(Key key, int modifiers = NoModifier);
    void sendText(const QString &text, int modifiers = NoModifier);
    // Wrapped in bracketed-paste markers when the application asked for them.
    void paste(const QString &text);

    // row in -scrollbackSize()..rows()-1
    Cell cell(int row, int column) const;
    QString lineText(int row) const;

    int cursorRow() const;
    int cursorColumn() const;
    bool isCursorVisible() const;
    // Full-screen programs (vim, htop) draw on the alternate screen, which
    // has no scrollback.
    bool isAltScreen() const;

signals:
    // Bytes for the backend: encoded keys and answers to terminal queries.
    void output(const QByteArray &data);
    // The screen changed; emitted at most once per feed() or key.
    void damaged();

private:
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int onPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int onPopLine(int cols, VTermScreenCell *cells, void *user);
    static int onClearScrollback(void *user);
    static void onOutput(const char *s, size_t len, void *user);

    Cell convert(const VTermScreenCell &c) const;
    void flush();

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    QList<QVector<VTermScreenCell> > m_scrollback;   // last is newest
    int m_scrollbackLimit = 1000;
    Rgb m_defaultFg;
    Rgb m_defaultBg;
    int m_cursorRow = 0;
    int m_cursorColumn = 0;
    bool m_cursorVisible = true;
    bool m_altScreen = false;
    bool m_damaged = false;
};

#endif // PVEDECK_TERMINALSCREEN_H
