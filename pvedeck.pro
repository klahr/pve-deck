TEMPLATE = subdirs
SUBDIRS = core app tests
app.depends = core
tests.depends = core

OTHER_FILES += rpm/pvedeck.spec PLAN.md README.md
