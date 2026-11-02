# Include from any project that links against the core library.
CORE_BUILD_DIR = $$shadowed($$PWD)
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
LIBS += -L$$CORE_BUILD_DIR -lpvedeckcore
PRE_TARGETDEPS += $$CORE_BUILD_DIR/libpvedeckcore.a

# libvterm, used by TerminalScreen. Link order: core first, then vterm.
VTERM_DIR = $$PWD/../3rdparty/libvterm
VTERM_BUILD_DIR = $$shadowed($$VTERM_DIR)
INCLUDEPATH += $$VTERM_DIR/include
LIBS += -L$$VTERM_BUILD_DIR -lvterm
PRE_TARGETDEPS += $$VTERM_BUILD_DIR/libvterm.a
