# libvterm 0.3.3 (MIT, https://www.leonerd.org.uk/code/libvterm/), unmodified.
# Built as a static library and linked into the app; see LICENSE.
TEMPLATE = lib
CONFIG += staticlib warn_off
CONFIG -= qt
TARGET = vterm

QMAKE_CFLAGS += -std=c99

INCLUDEPATH += include

HEADERS += \
    include/vterm.h \
    include/vterm_keycodes.h \
    src/rect.h \
    src/utf8.h \
    src/vterm_internal.h

SOURCES += \
    src/encoding.c \
    src/keyboard.c \
    src/mouse.c \
    src/parser.c \
    src/pen.c \
    src/screen.c \
    src/state.c \
    src/unicode.c \
    src/vterm.c
