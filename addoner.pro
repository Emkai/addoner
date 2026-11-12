QT += core gui network qml quick quickcontrols2

CONFIG += c++17 release
win32: CONFIG += windows
TARGET = addoner
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/manager.h \
    src/theme.h \
    src/translator.h \
    src/webchecker.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/manager.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/webchecker.cpp

RESOURCES += src/resources.qrc
