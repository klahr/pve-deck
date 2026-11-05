// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_TERMINALITEM_H
#define PVEDECK_TERMINALITEM_H

#include <QColor>
#include <QFont>
#include <QPointer>
#include <QQuickPaintedItem>

#include "terminalbackend.h"
#include "terminalscreen.h"

// Draws a TerminalScreen and turns key presses (hardware or the on-screen
// keyboard) into terminal input for the backend. The screen size in
// characters follows the item size and font size, and is passed on to
// the backend so full-screen programs reflow on rotation.
//
// Selections are kept in absolute lines (scrollback first, then the
// screen), so they stay on the same text while scrolling.
class TerminalItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(TerminalBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(int columns READ columns NOTIFY terminalSizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY terminalSizeChanged)
    Q_PROPERTY(qreal cellHeight READ cellHeight NOTIFY fontSizeChanged)
    // Lines scrolled back into history; 0 shows the live screen. Always 0
    // while a full-screen program uses the alternate screen.
    Q_PROPERTY(int scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollChanged)
    // Latched modifiers apply to the next key only; locked ones to every
    // key until unlocked.
    Q_PROPERTY(bool ctrlLatched READ ctrlLatched WRITE setCtrlLatched NOTIFY modifiersChanged)
    Q_PROPERTY(bool altLatched READ altLatched WRITE setAltLatched NOTIFY modifiersChanged)
    Q_PROPERTY(bool ctrlLocked READ ctrlLocked WRITE setCtrlLocked NOTIFY modifiersChanged)
    Q_PROPERTY(bool altLocked READ altLocked WRITE setAltLocked NOTIFY modifiersChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY selectionColorChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);

    TerminalBackend *backend() const;
    void setBackend(TerminalBackend *backend);
    qreal fontSize() const;
    void setFontSize(qreal size);
    int columns() const;
    int rows() const;
    qreal cellHeight() const;
    int scrollOffset() const;
    void setScrollOffset(int offset);
    bool ctrlLatched() const;
    void setCtrlLatched(bool latched);
    bool altLatched() const;
    void setAltLatched(bool latched);
    bool ctrlLocked() const;
    void setCtrlLocked(bool locked);
    bool altLocked() const;
    void setAltLocked(bool locked);
    bool hasSelection() const;
    QColor selectionColor() const;
    void setSelectionColor(const QColor &color);

    // "Escape", "Tab", "Enter", "Backspace", "Up", "Down", "Left", "Right",
    // "Home", "End", "PageUp", "PageDown", "Insert", "Delete", "F1".."F12"
    Q_INVOKABLE void sendKey(const QString &name);
    Q_INVOKABLE void sendText(const QString &text);
    Q_INVOKABLE void paste(const QString &text);

    // x and y in item coordinates.
    Q_INVOKABLE void startSelection(qreal x, qreal y);
    Q_INVOKABLE void updateSelection(qreal x, qreal y);
    Q_INVOKABLE bool selectWordAt(qreal x, qreal y);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString selectedText() const;
    // The selection if it is a web link, otherwise empty.
    Q_INVOKABLE QString selectedUrl() const;

    void paint(QPainter *painter) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void backendChanged();
    void fontSizeChanged();
    void terminalSizeChanged();
    void scrollChanged();
    void modifiersChanged();
    void selectionChanged();
    void selectionColorChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void updateMetrics();
    void updateTerminalSize();
    int takeModifiers(Qt::KeyboardModifiers held = Qt::NoModifier);
    void sendCharacters(const QString &text, int modifiers);
    void userInput();
    // Absolute line: 0 is the oldest scrollback line.
    void cellAt(qreal x, qreal y, int *line, int *column) const;
    TerminalScreen::Cell cellAtLine(int line, int column) const;
    void orderedSelection(int *startLine, int *startColumn, int *endLine, int *endColumn) const;
    bool isSelected(int line, int column) const;

    TerminalScreen *m_screen;
    QPointer<TerminalBackend> m_backend;
    QFont m_font;
    qreal m_fontSize = 0;
    qreal m_cellWidth = 1;
    qreal m_cellHeight = 1;
    qreal m_ascent = 0;
    int m_scrollOffset = 0;
    int m_lastScrollback = 0;
    int m_lastDropped = 0;
    bool m_ctrlLatched = false;
    bool m_altLatched = false;
    bool m_ctrlLocked = false;
    bool m_altLocked = false;
    bool m_hasSelection = false;
    int m_anchorLine = 0;
    int m_anchorColumn = 0;
    int m_endLine = 0;
    int m_endColumn = 0;
    QColor m_selectionColor;
};

#endif // PVEDECK_TERMINALITEM_H
