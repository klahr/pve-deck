// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminalitem.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRegExp>

#include <cmath>

namespace {

QColor toColor(TerminalScreen::Rgb rgb)
{
    return QColor::fromRgb(static_cast<QRgb>(rgb));
}

bool sameStyle(const TerminalScreen::Cell &a, const TerminalScreen::Cell &b)
{
    return a.foreground == b.foreground && a.bold == b.bold && a.italic == b.italic
            && a.underline == b.underline && a.strike == b.strike;
}

// Characters that end a word for double-tap selection.
bool isWordCell(const TerminalScreen::Cell &cell)
{
    if (cell.width == 0)
        return true;   // second half of a wide character
    if (cell.text.isEmpty() || cell.text == QLatin1String(" ") || cell.text == QLatin1String("\t"))
        return false;
    static const QString delimiters = QStringLiteral("\"'`()[]{}<>|;,");
    return !(cell.text.size() == 1 && delimiters.contains(cell.text.at(0)));
}

const QHash<QString, TerminalScreen::Key> &keyNames()
{
    static QHash<QString, TerminalScreen::Key> names;
    if (names.isEmpty()) {
        names.insert(QStringLiteral("Escape"), TerminalScreen::KeyEscape);
        names.insert(QStringLiteral("Tab"), TerminalScreen::KeyTab);
        names.insert(QStringLiteral("Enter"), TerminalScreen::KeyEnter);
        names.insert(QStringLiteral("Backspace"), TerminalScreen::KeyBackspace);
        names.insert(QStringLiteral("Up"), TerminalScreen::KeyUp);
        names.insert(QStringLiteral("Down"), TerminalScreen::KeyDown);
        names.insert(QStringLiteral("Left"), TerminalScreen::KeyLeft);
        names.insert(QStringLiteral("Right"), TerminalScreen::KeyRight);
        names.insert(QStringLiteral("Home"), TerminalScreen::KeyHome);
        names.insert(QStringLiteral("End"), TerminalScreen::KeyEnd);
        names.insert(QStringLiteral("PageUp"), TerminalScreen::KeyPageUp);
        names.insert(QStringLiteral("PageDown"), TerminalScreen::KeyPageDown);
        names.insert(QStringLiteral("Insert"), TerminalScreen::KeyInsert);
        names.insert(QStringLiteral("Delete"), TerminalScreen::KeyDelete);
        for (int i = 0; i < 12; ++i)
            names.insert(QStringLiteral("F%1").arg(i + 1),
                         static_cast<TerminalScreen::Key>(TerminalScreen::KeyF1 + i));
    }
    return names;
}

bool qtKeyToTerminal(int key, TerminalScreen::Key *out)
{
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: *out = TerminalScreen::KeyEnter; return true;
    case Qt::Key_Tab: *out = TerminalScreen::KeyTab; return true;
    case Qt::Key_Backspace: *out = TerminalScreen::KeyBackspace; return true;
    case Qt::Key_Escape: *out = TerminalScreen::KeyEscape; return true;
    case Qt::Key_Up: *out = TerminalScreen::KeyUp; return true;
    case Qt::Key_Down: *out = TerminalScreen::KeyDown; return true;
    case Qt::Key_Left: *out = TerminalScreen::KeyLeft; return true;
    case Qt::Key_Right: *out = TerminalScreen::KeyRight; return true;
    case Qt::Key_Home: *out = TerminalScreen::KeyHome; return true;
    case Qt::Key_End: *out = TerminalScreen::KeyEnd; return true;
    case Qt::Key_PageUp: *out = TerminalScreen::KeyPageUp; return true;
    case Qt::Key_PageDown: *out = TerminalScreen::KeyPageDown; return true;
    case Qt::Key_Insert: *out = TerminalScreen::KeyInsert; return true;
    case Qt::Key_Delete: *out = TerminalScreen::KeyDelete; return true;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
            *out = static_cast<TerminalScreen::Key>(TerminalScreen::KeyF1 + (key - Qt::Key_F1));
            return true;
        }
        return false;
    }
}

} // namespace

TerminalItem::TerminalItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_screen(new TerminalScreen(24, 80, this))
    , m_font(QFontDatabase::systemFont(QFontDatabase::FixedFont))
    , m_selectionColor(QColor(255, 255, 255, 80))
{
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemHasContents, true);
    setOpaquePainting(true);
    m_font.setStyleHint(QFont::TypeWriter);
    m_font.setFixedPitch(true);
    m_fontSize = 24;
    updateMetrics();

    connect(m_screen, &TerminalScreen::damaged, this, [this]() {
        const int size = m_screen->scrollbackSize();
        if (m_screen->isAltScreen()) {
            setScrollOffset(0);
        } else if (size != m_lastScrollback && m_scrollOffset > 0) {
            // Keep the view on the same history line while scrolled back.
            setScrollOffset(m_scrollOffset + qMax(0, size - m_lastScrollback));
        }
        m_lastScrollback = size;
        update();
    });
    connect(m_screen, &TerminalScreen::output, this, [this](const QByteArray &data) {
        if (m_backend)
            m_backend->write(data);
    });
}

TerminalBackend *TerminalItem::backend() const
{
    return m_backend;
}

void TerminalItem::setBackend(TerminalBackend *backend)
{
    if (backend == m_backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (backend) {
        connect(backend, &TerminalBackend::dataReceived, m_screen, &TerminalScreen::feed);
        backend->resize(m_screen->columns(), m_screen->rows());
    }
    emit backendChanged();
}

qreal TerminalItem::fontSize() const
{
    return m_fontSize;
}

void TerminalItem::setFontSize(qreal size)
{
    size = qBound<qreal>(8, size, 96);
    if (qFuzzyCompare(size, m_fontSize))
        return;
    m_fontSize = size;
    updateMetrics();
    updateTerminalSize();
    update();
    emit fontSizeChanged();
}

int TerminalItem::columns() const
{
    return m_screen->columns();
}

int TerminalItem::rows() const
{
    return m_screen->rows();
}

qreal TerminalItem::cellHeight() const
{
    return m_cellHeight;
}

int TerminalItem::scrollOffset() const
{
    return m_scrollOffset;
}

void TerminalItem::setScrollOffset(int offset)
{
    const int maximum = m_screen->isAltScreen() ? 0 : m_screen->scrollbackSize();
    offset = qBound(0, offset, maximum);
    if (offset == m_scrollOffset)
        return;
    m_scrollOffset = offset;
    update();
    emit scrollChanged();
}

bool TerminalItem::ctrlLatched() const
{
    return m_ctrlLatched;
}

void TerminalItem::setCtrlLatched(bool latched)
{
    if (latched == m_ctrlLatched)
        return;
    m_ctrlLatched = latched;
    emit modifiersChanged();
}

bool TerminalItem::altLatched() const
{
    return m_altLatched;
}

void TerminalItem::setAltLatched(bool latched)
{
    if (latched == m_altLatched)
        return;
    m_altLatched = latched;
    emit modifiersChanged();
}

bool TerminalItem::ctrlLocked() const
{
    return m_ctrlLocked;
}

void TerminalItem::setCtrlLocked(bool locked)
{
    if (locked == m_ctrlLocked)
        return;
    m_ctrlLocked = locked;
    emit modifiersChanged();
}

bool TerminalItem::altLocked() const
{
    return m_altLocked;
}

void TerminalItem::setAltLocked(bool locked)
{
    if (locked == m_altLocked)
        return;
    m_altLocked = locked;
    emit modifiersChanged();
}

bool TerminalItem::hasSelection() const
{
    return m_hasSelection;
}

QColor TerminalItem::selectionColor() const
{
    return m_selectionColor;
}

void TerminalItem::setSelectionColor(const QColor &color)
{
    if (color == m_selectionColor)
        return;
    m_selectionColor = color;
    if (m_hasSelection)
        update();
    emit selectionColorChanged();
}

void TerminalItem::sendKey(const QString &name)
{
    const QHash<QString, TerminalScreen::Key> &names = keyNames();
    if (!names.contains(name))
        return;
    m_screen->sendKey(names.value(name), takeModifiers());
    userInput();
}

void TerminalItem::sendText(const QString &text)
{
    if (text.isEmpty())
        return;
    sendCharacters(text, takeModifiers());
    userInput();
}

void TerminalItem::paste(const QString &text)
{
    if (text.isEmpty())
        return;
    m_screen->paste(text);
    userInput();
}

void TerminalItem::startSelection(qreal x, qreal y)
{
    cellAt(x, y, &m_anchorLine, &m_anchorColumn);
    m_endLine = m_anchorLine;
    m_endColumn = m_anchorColumn;
    m_hasSelection = true;
    update();
    emit selectionChanged();
}

void TerminalItem::updateSelection(qreal x, qreal y)
{
    if (!m_hasSelection)
        return;
    cellAt(x, y, &m_endLine, &m_endColumn);
    update();
    emit selectionChanged();
}

bool TerminalItem::selectWordAt(qreal x, qreal y)
{
    int line;
    int column;
    cellAt(x, y, &line, &column);
    // Start from the left half when the right half of a wide character was hit.
    if (column > 0 && cellAtLine(line, column).width == 0)
        --column;
    if (!isWordCell(cellAtLine(line, column)))
        return false;

    const int columns = m_screen->columns();
    int first = column;
    while (first > 0 && isWordCell(cellAtLine(line, first - 1)))
        --first;
    int last = column;
    while (last < columns - 1 && isWordCell(cellAtLine(line, last + 1)))
        ++last;

    m_anchorLine = line;
    m_anchorColumn = first;
    m_endLine = line;
    m_endColumn = last;
    m_hasSelection = true;
    update();
    emit selectionChanged();
    return true;
}

void TerminalItem::clearSelection()
{
    if (!m_hasSelection)
        return;
    m_hasSelection = false;
    update();
    emit selectionChanged();
}

QString TerminalItem::selectedText() const
{
    if (!m_hasSelection)
        return QString();
    int startLine, startColumn, endLine, endColumn;
    orderedSelection(&startLine, &startColumn, &endLine, &endColumn);
    const int columns = m_screen->columns();

    QStringList lines;
    for (int line = startLine; line <= endLine; ++line) {
        const int first = line == startLine ? startColumn : 0;
        const int last = line == endLine ? endColumn : columns - 1;
        QString text;
        for (int column = first; column <= last; ++column) {
            const TerminalScreen::Cell cell = cellAtLine(line, column);
            if (cell.width == 0)
                continue;
            text += cell.text.isEmpty() ? QStringLiteral(" ") : cell.text;
        }
        // Blank cells past the end of the text are not part of the line.
        while (text.endsWith(QLatin1Char(' ')))
            text.chop(1);
        lines.append(text);
    }
    return lines.join(QLatin1Char('\n'));
}

QString TerminalItem::selectedUrl() const
{
    const QString text = selectedText().trimmed();
    static const QRegExp url(QStringLiteral("^(https?|ftp)://\\S+$"), Qt::CaseInsensitive);
    return url.exactMatch(text) ? text : QString();
}

void TerminalItem::paint(QPainter *painter)
{
    const QColor defaultBg = toColor(m_screen->defaultBackground());
    painter->fillRect(boundingRect(), defaultBg);

    const int rows = m_screen->rows();
    const int cols = m_screen->columns();
    const int scrollback = m_screen->scrollbackSize();
    const bool showCursor = m_scrollOffset == 0 && m_screen->isCursorVisible();

    for (int r = 0; r < rows; ++r) {
        const int row = r - m_scrollOffset;
        const qreal y = r * m_cellHeight;

        QVector<TerminalScreen::Cell> cells(cols);
        for (int c = 0; c < cols; ++c)
            cells[c] = m_screen->cell(row, c);

        // Backgrounds, merged into runs.
        int c = 0;
        while (c < cols) {
            const TerminalScreen::Cell &cell = cells.at(c);
            if (cell.defaultBackground) {
                ++c;
                continue;
            }
            int end = c + 1;
            while (end < cols && !cells.at(end).defaultBackground
                   && cells.at(end).background == cell.background)
                ++end;
            painter->fillRect(QRectF(c * m_cellWidth, y, (end - c) * m_cellWidth, m_cellHeight),
                              toColor(cell.background));
            c = end;
        }

        // The selection, under the text so it stays readable.
        if (m_hasSelection) {
            const int line = scrollback + row;
            int first = -1;
            int last = -1;
            for (int col = 0; col < cols; ++col) {
                if (isSelected(line, col)) {
                    if (first < 0)
                        first = col;
                    last = col;
                }
            }
            if (first >= 0)
                painter->fillRect(QRectF(first * m_cellWidth, y, (last - first + 1) * m_cellWidth, m_cellHeight),
                                  m_selectionColor);
        }

        // Text, in runs of equal style. Wide characters are drawn alone so
        // the following cells stay on the grid.
        c = 0;
        while (c < cols) {
            const TerminalScreen::Cell &cell = cells.at(c);
            if (cell.width == 0 || cell.text.isEmpty() || cell.text == QLatin1String(" ")) {
                ++c;
                continue;
            }
            QString text = cell.text;
            int end = c + 1;
            if (cell.width == 1) {
                while (end < cols && cells.at(end).width == 1 && !cells.at(end).text.isEmpty()
                       && sameStyle(cells.at(end), cell)) {
                    text += cells.at(end).text;
                    ++end;
                }
            } else {
                end = c + cell.width;
            }
            QFont font = m_font;
            font.setBold(cell.bold);
            font.setItalic(cell.italic);
            font.setUnderline(cell.underline);
            font.setStrikeOut(cell.strike);
            painter->setFont(font);
            painter->setPen(toColor(cell.foreground));
            painter->drawText(QPointF(c * m_cellWidth, y + m_ascent), text);
            c = end;
        }

        if (showCursor && r == m_screen->cursorRow()) {
            const int cc = qMin(m_screen->cursorColumn(), cols - 1);
            const TerminalScreen::Cell &cell = cells.at(cc);
            const QRectF rect(cc * m_cellWidth, y, qMax(1, cell.width) * m_cellWidth, m_cellHeight);
            painter->fillRect(rect, toColor(cell.foreground));
            if (!cell.text.isEmpty()) {
                painter->setFont(m_font);
                painter->setPen(cell.defaultBackground ? defaultBg : toColor(cell.background));
                painter->drawText(QPointF(rect.x(), y + m_ascent), cell.text);
            }
        }
    }
}

QVariant TerminalItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        // No prediction or auto-capitals: every key goes straight to the shell.
        return static_cast<int>(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase
                                | Qt::ImhPreferLowercase);
    case Qt::ImCursorRectangle:
        return QRectF(m_screen->cursorColumn() * m_cellWidth, m_screen->cursorRow() * m_cellHeight,
                      m_cellWidth, m_cellHeight);
    case Qt::ImSurroundingText:
        return QString();
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    default:
        return QQuickPaintedItem::inputMethodQuery(query);
    }
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    // Ctrl+Shift+C/V copy and paste, as in desktop terminals.
    const Qt::KeyboardModifiers held = event->modifiers()
            & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    if (held == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_C) {
            if (m_hasSelection)
                QGuiApplication::clipboard()->setText(selectedText());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_V) {
            paste(QGuiApplication::clipboard()->text());
            event->accept();
            return;
        }
    }

    // Meta (Super) works as Alt: many keyboards have no right Alt.
    Qt::KeyboardModifiers eventModifiers = event->modifiers();
    if (eventModifiers & Qt::MetaModifier)
        eventModifiers |= Qt::AltModifier;
    const int key = event->key();
    const QString text = event->text();

    TerminalScreen::Key terminalKey;
    if (qtKeyToTerminal(key, &terminalKey)) {
        m_screen->sendKey(terminalKey, takeModifiers(eventModifiers));
    } else {
        const int modifiers = takeModifiers(eventModifiers);
        if ((modifiers & TerminalScreen::CtrlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
            // With Ctrl held, text() is already a control character.
            m_screen->sendText(QString(QChar('a' + (key - Qt::Key_A))), modifiers);
        } else if ((modifiers & TerminalScreen::CtrlModifier) && key >= Qt::Key_Space
                   && key <= Qt::Key_AsciiTilde) {
            // Ctrl with [ \ ] and the like, which the key code says better than text().
            m_screen->sendText(QString(QChar(key)), modifiers);
        } else if ((modifiers & TerminalScreen::AltModifier) && key >= Qt::Key_Space
                   && key <= Qt::Key_AsciiTilde
                   && (text.isEmpty() || text.at(0).unicode() > 0x7e)) {
            // Alt types a special character on some layouts.
            sendCharacters(QString(QChar(key)).toLower(), modifiers);
        } else if (!text.isEmpty()) {
            sendCharacters(text, modifiers);
        } else {
            QQuickPaintedItem::keyPressEvent(event);
            return;
        }
    }
    event->accept();
    userInput();
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty()) {
        sendCharacters(event->commitString(), takeModifiers());
        userInput();
    }
    event->accept();
}

void TerminalItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        updateTerminalSize();
}

void TerminalItem::updateMetrics()
{
    m_font.setPixelSize(qRound(m_fontSize));
    const QFontMetricsF metrics(m_font);
    m_cellWidth = qMax<qreal>(1, metrics.width(QLatin1Char('M')));
    m_cellHeight = qMax<qreal>(1, std::ceil(metrics.height()));
    m_ascent = metrics.ascent();
}

void TerminalItem::updateTerminalSize()
{
    if (width() <= 0 || height() <= 0)
        return;
    const int cols = qMax(2, static_cast<int>(width() / m_cellWidth));
    const int rows = qMax(2, static_cast<int>(height() / m_cellHeight));
    if (cols == m_screen->columns() && rows == m_screen->rows())
        return;
    // Text moves around on resize, so an old selection would be wrong.
    clearSelection();
    m_screen->setSize(rows, cols);
    if (m_backend)
        m_backend->resize(cols, rows);
    setScrollOffset(m_scrollOffset);
    emit terminalSizeChanged();
    update();
}

int TerminalItem::takeModifiers(Qt::KeyboardModifiers held)
{
    int mods = TerminalScreen::NoModifier;
    if (held & Qt::ShiftModifier)
        mods |= TerminalScreen::ShiftModifier;
    if ((held & Qt::ControlModifier) || m_ctrlLatched || m_ctrlLocked)
        mods |= TerminalScreen::CtrlModifier;
    if ((held & Qt::AltModifier) || m_altLatched || m_altLocked)
        mods |= TerminalScreen::AltModifier;
    setCtrlLatched(false);
    setAltLatched(false);
    return mods;
}

void TerminalItem::sendCharacters(const QString &text, int modifiers)
{
    // The on-screen keyboard's Enter can arrive as a newline.
    QString chunk;
    foreach (const QChar c, text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            m_screen->sendText(chunk, modifiers);
            chunk.clear();
            m_screen->sendKey(TerminalScreen::KeyEnter, modifiers);
        } else {
            chunk += c;
        }
    }
    m_screen->sendText(chunk, modifiers);
}

void TerminalItem::userInput()
{
    // Typing goes back to the live screen and drops the selection.
    clearSelection();
    setScrollOffset(0);
}

void TerminalItem::cellAt(qreal x, qreal y, int *line, int *column) const
{
    const int row = static_cast<int>(qBound<qreal>(0, y / m_cellHeight, m_screen->rows() - 1));
    *column = static_cast<int>(qBound<qreal>(0, x / m_cellWidth, m_screen->columns() - 1));
    *line = m_screen->scrollbackSize() - m_scrollOffset + row;
}

TerminalScreen::Cell TerminalItem::cellAtLine(int line, int column) const
{
    return m_screen->cell(line - m_screen->scrollbackSize(), column);
}

void TerminalItem::orderedSelection(int *startLine, int *startColumn, int *endLine, int *endColumn) const
{
    const bool anchorFirst = m_anchorLine < m_endLine
            || (m_anchorLine == m_endLine && m_anchorColumn <= m_endColumn);
    *startLine = anchorFirst ? m_anchorLine : m_endLine;
    *startColumn = anchorFirst ? m_anchorColumn : m_endColumn;
    *endLine = anchorFirst ? m_endLine : m_anchorLine;
    *endColumn = anchorFirst ? m_endColumn : m_anchorColumn;
}

bool TerminalItem::isSelected(int line, int column) const
{
    int startLine, startColumn, endLine, endColumn;
    orderedSelection(&startLine, &startColumn, &endLine, &endColumn);
    if (line < startLine || line > endLine)
        return false;
    if (line == startLine && column < startColumn)
        return false;
    if (line == endLine && column > endColumn)
        return false;
    return true;
}
