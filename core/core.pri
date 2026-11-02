# Include from any project that links against the core library.
CORE_BUILD_DIR = $$shadowed($$PWD)
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
LIBS += -L$$CORE_BUILD_DIR -lpvedeckcore
PRE_TARGETDEPS += $$CORE_BUILD_DIR/libpvedeckcore.a
