// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminalscreen.h"

#include <cstring>

namespace {

const uint32_t WideContinuation = static_cast<uint32_t>(-1);

TerminalScreen::Rgb makeRgb(int r, int g, int b)
{
    return 0xff000000u | (quint32(r & 0xff) << 16) | (quint32(g & 0xff) << 8) | quint32(b & 0xff);
}

VTermScreenCallbacks callbacks;

VTermColor toVTerm(TerminalScreen::Rgb rgb)
{
    VTermColor c;
    vterm_color_rgb(&c, static_cast<uint8_t>((rgb >> 16) & 0xff), static_cast<uint8_t>((rgb >> 8) & 0xff),
                    static_cast<uint8_t>(rgb & 0xff));
    return c;
}

VTermKey toVTerm(TerminalScreen::Key key)
{
    switch (key) {
    case TerminalScreen::KeyEnter: return VTERM_KEY_ENTER;
    case TerminalScreen::KeyTab: return VTERM_KEY_TAB;
    case TerminalScreen::KeyBackspace: return VTERM_KEY_BACKSPACE;
    case TerminalScreen::KeyEscape: return VTERM_KEY_ESCAPE;
    case TerminalScreen::KeyUp: return VTERM_KEY_UP;
    case TerminalScreen::KeyDown: return VTERM_KEY_DOWN;
    case TerminalScreen::KeyLeft: return VTERM_KEY_LEFT;
    case TerminalScreen::KeyRight: return VTERM_KEY_RIGHT;
    case TerminalScreen::KeyInsert: return VTERM_KEY_INS;
    case TerminalScreen::KeyDelete: return VTERM_KEY_DEL;
    case TerminalScreen::KeyHome: return VTERM_KEY_HOME;
    case TerminalScreen::KeyEnd: return VTERM_KEY_END;
    case TerminalScreen::KeyPageUp: return VTERM_KEY_PAGEUP;
    case TerminalScreen::KeyPageDown: return VTERM_KEY_PAGEDOWN;
    default:
        return static_cast<VTermKey>(VTERM_KEY_FUNCTION(1 + key - TerminalScreen::KeyF1));
    }
}

} // namespace

TerminalScreen::TerminalScreen(int rows, int columns, QObject *parent)
    : QObject(parent)
    , m_defaultFg(makeRgb(0xe0, 0xe0, 0xe0))
    , m_defaultBg(makeRgb(0, 0, 0))
{
    callbacks.damage = &TerminalScreen::onDamage;
    callbacks.movecursor = &TerminalScreen::onMoveCursor;
    callbacks.settermprop = &TerminalScreen::onSetTermProp;
    callbacks.sb_pushline = &TerminalScreen::onPushLine;
    callbacks.sb_popline = &TerminalScreen::onPopLine;
    callbacks.sb_clear = &TerminalScreen::onClearScrollback;

    m_vt = vterm_new(qMax(1, rows), qMax(1, columns));
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, &TerminalScreen::onOutput, this);

    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
    const VTermColor fg = toVTerm(m_defaultFg);
    const VTermColor bg = toVTerm(m_defaultBg);
    vterm_screen_set_default_colors(m_screen, &fg, &bg);
    vterm_screen_reset(m_screen, 1);
}

TerminalScreen::~TerminalScreen()
{
    vterm_free(m_vt);
}

int TerminalScreen::rows() const
{
    int rows = 0;
    int cols = 0;
    vterm_get_size(m_vt, &rows, &cols);
    return rows;
}

int TerminalScreen::columns() const
{
    int rows = 0;
    int cols = 0;
    vterm_get_size(m_vt, &rows, &cols);
    return cols;
}

void TerminalScreen::setSize(int rows, int columns)
{
    rows = qMax(1, rows);
    columns = qMax(1, columns);
    if (rows == this->rows() && columns == this->columns())
        return;
    vterm_set_size(m_vt, rows, columns);
    flush();
}

void TerminalScreen::setDefaultColors(Rgb foreground, Rgb background)
{
    m_defaultFg = foreground;
    m_defaultBg = background;
    const VTermColor fg = toVTerm(foreground);
    const VTermColor bg = toVTerm(background);
    vterm_screen_set_default_colors(m_screen, &fg, &bg);
    m_damaged = true;
    flush();
}

TerminalScreen::Rgb TerminalScreen::defaultForeground() const
{
    return m_defaultFg;
}

TerminalScreen::Rgb TerminalScreen::defaultBackground() const
{
    return m_defaultBg;
}

void TerminalScreen::setScrollbackLimit(int lines)
{
    m_scrollbackLimit = qMax(0, lines);
    while (m_scrollback.size() > m_scrollbackLimit) {
        m_scrollback.removeFirst();
        ++m_droppedLines;
    }
}

int TerminalScreen::scrollbackSize() const
{
    return m_scrollback.size();
}

int TerminalScreen::droppedLines() const
{
    return m_droppedLines;
}

void TerminalScreen::feed(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    vterm_input_write(m_vt, data.constData(), static_cast<size_t>(data.size()));
    flush();
}

void TerminalScreen::sendKey(Key key, int modifiers)
{
    vterm_keyboard_key(m_vt, toVTerm(key), static_cast<VTermModifier>(modifiers));
}

void TerminalScreen::sendText(const QString &text, int modifiers)
{
    foreach (uint c, text.toUcs4()) {
        // Ctrl+letter is sent as the control character; libvterm does that
        // for lower case only.
        if ((modifiers & CtrlModifier) && c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        vterm_keyboard_unichar(m_vt, c, static_cast<VTermModifier>(modifiers));
    }
}

void TerminalScreen::paste(const QString &text)
{
    vterm_keyboard_start_paste(m_vt);
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    normalized.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    const QByteArray utf8 = normalized.toUtf8();
    if (!utf8.isEmpty())
        emit output(utf8);
    vterm_keyboard_end_paste(m_vt);
}

TerminalScreen::Cell TerminalScreen::cell(int row, int column) const
{
    if (row < 0) {
        const int index = m_scrollback.size() + row;
        if (index < 0)
            return Cell();
        const QVector<VTermScreenCell> &line = m_scrollback.at(index);
        if (column < 0 || column >= line.size()) {
            Cell blank;
            blank.foreground = m_defaultFg;
            blank.background = m_defaultBg;
            return blank;
        }
        return convert(line.at(column));
    }
    VTermScreenCell c;
    VTermPos pos;
    pos.row = row;
    pos.col = column;
    if (!vterm_screen_get_cell(m_screen, pos, &c))
        return Cell();
    return convert(c);
}

QString TerminalScreen::lineText(int row) const
{
    QString out;
    const int cols = row < 0 && m_scrollback.size() + row >= 0
            ? m_scrollback.at(m_scrollback.size() + row).size() : columns();
    for (int col = 0; col < cols; ++col) {
        const Cell c = cell(row, col);
        if (c.width == 0)
            continue;
        out += c.text.isEmpty() ? QStringLiteral(" ") : c.text;
    }
    // Trailing blanks are padding, not content.
    int end = out.size();
    while (end > 0 && out.at(end - 1) == QLatin1Char(' '))
        --end;
    return out.left(end);
}

int TerminalScreen::cursorRow() const
{
    return m_cursorRow;
}

int TerminalScreen::cursorColumn() const
{
    return m_cursorColumn;
}

bool TerminalScreen::isCursorVisible() const
{
    return m_cursorVisible;
}

bool TerminalScreen::isAltScreen() const
{
    return m_altScreen;
}

int TerminalScreen::onDamage(VTermRect, void *user)
{
    static_cast<TerminalScreen *>(user)->m_damaged = true;
    return 1;
}

int TerminalScreen::onMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
    TerminalScreen *self = static_cast<TerminalScreen *>(user);
    self->m_cursorRow = pos.row;
    self->m_cursorColumn = pos.col;
    self->m_cursorVisible = visible;
    self->m_damaged = true;
    return 1;
}

int TerminalScreen::onSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    TerminalScreen *self = static_cast<TerminalScreen *>(user);
    if (prop == VTERM_PROP_CURSORVISIBLE) {
        self->m_cursorVisible = val->boolean;
        self->m_damaged = true;
    } else if (prop == VTERM_PROP_ALTSCREEN) {
        self->m_altScreen = val->boolean;
        self->m_damaged = true;
    }
    return 1;
}

int TerminalScreen::onPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    TerminalScreen *self = static_cast<TerminalScreen *>(user);
    if (self->m_scrollbackLimit == 0)
        return 1;
    QVector<VTermScreenCell> line(cols);
    std::memcpy(line.data(), cells, sizeof(VTermScreenCell) * static_cast<size_t>(cols));
    self->m_scrollback.append(line);
    if (self->m_scrollback.size() > self->m_scrollbackLimit) {
        self->m_scrollback.removeFirst();
        ++self->m_droppedLines;
    }
    return 1;
}

int TerminalScreen::onPopLine(int cols, VTermScreenCell *cells, void *user)
{
    TerminalScreen *self = static_cast<TerminalScreen *>(user);
    if (self->m_scrollback.isEmpty())
        return 0;
    const QVector<VTermScreenCell> line = self->m_scrollback.takeLast();
    const int n = qMin(cols, line.size());
    std::memcpy(cells, line.constData(), sizeof(VTermScreenCell) * static_cast<size_t>(n));
    if (n < cols) {
        VTermScreenCell blank = line.isEmpty() ? VTermScreenCell() : line.last();
        std::memset(blank.chars, 0, sizeof(blank.chars));
        blank.width = 1;
        for (int i = n; i < cols; ++i)
            cells[i] = blank;
    }
    return 1;
}

int TerminalScreen::onClearScrollback(void *user)
{
    TerminalScreen *self = static_cast<TerminalScreen *>(user);
    self->m_droppedLines += self->m_scrollback.size();
    self->m_scrollback.clear();
    self->m_damaged = true;
    return 1;
}

void TerminalScreen::onOutput(const char *s, size_t len, void *user)
{
    emit static_cast<TerminalScreen *>(user)->output(QByteArray(s, static_cast<int>(len)));
}

TerminalScreen::Cell TerminalScreen::convert(const VTermScreenCell &c) const
{
    Cell out;
    if (c.chars[0] == WideContinuation) {
        out.width = 0;
    } else {
        uint ucs[VTERM_MAX_CHARS_PER_CELL];
        int n = 0;
        while (n < VTERM_MAX_CHARS_PER_CELL && c.chars[n] != 0) {
            ucs[n] = c.chars[n];
            ++n;
        }
        out.text = QString::fromUcs4(ucs, n);
        out.width = c.width;
    }
    out.bold = c.attrs.bold;
    out.italic = c.attrs.italic;
    out.underline = c.attrs.underline != 0;
    out.strike = c.attrs.strike;

    // The "default" flags are lost by the RGB conversion: check first.
    VTermColor fg = c.fg;
    VTermColor bg = c.bg;
    const bool defaultFg = VTERM_COLOR_IS_DEFAULT_FG(&fg);
    const bool defaultBg = VTERM_COLOR_IS_DEFAULT_BG(&bg);
    vterm_screen_convert_color_to_rgb(m_screen, &fg);
    vterm_screen_convert_color_to_rgb(m_screen, &bg);
    out.foreground = defaultFg ? m_defaultFg : makeRgb(fg.rgb.red, fg.rgb.green, fg.rgb.blue);
    out.background = defaultBg ? m_defaultBg : makeRgb(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
    out.defaultBackground = defaultBg;

    if (c.attrs.reverse) {
        qSwap(out.foreground, out.background);
        out.defaultBackground = false;
    }
    return out;
}

void TerminalScreen::flush()
{
    vterm_screen_flush_damage(m_screen);
    if (m_damaged) {
        m_damaged = false;
        emit damaged();
    }
}
