QT += core gui network quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = addoner_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/manager.h ../src/theme.h ../src/translator.h ../src/webchecker.h
SOURCES += addoner_tests.cpp ../src/manager.cpp ../src/theme.cpp ../src/translator.cpp ../src/webchecker.cpp
