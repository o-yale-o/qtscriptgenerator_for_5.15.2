TARGET = qtscript_xmlpatterns
include(../qtbindingsbase.pri)
QT += core gui xmlpatterns network
SOURCES += plugin.cpp
HEADERS += plugin.h
INCLUDEPATH += ./include/
include($$GENERATEDCPP/com_trolltech_qt_xmlpatterns/com_trolltech_qt_xmlpatterns.pri)
