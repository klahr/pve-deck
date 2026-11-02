TEMPLATE = subdirs
SUBDIRS = vterm core app tests
vterm.subdir = 3rdparty/libvterm
core.depends = vterm
app.depends = core
tests.depends = core

OTHER_FILES += rpm/pvedeck.spec PLAN.md README.md
