QT += testlib

VERSION = 1.0

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

SOURCES += tst_qviewtests.cpp

INCLUDEPATH += ../src
include(../src/src.pri)
