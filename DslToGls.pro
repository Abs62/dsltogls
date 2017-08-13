QT += core

CONFIG += c++11

TARGET = DslToGls
CONFIG += console
CONFIG -= app_bundle

OBJECTS_DIR = build

TEMPLATE = app

INCLUDEPATH += .
INCLUDEPATH += winlibs/include

LIBS += -L$${PWD}/winlibs/lib
LIBS += -liconv \
        -lz

SOURCES += \
    main.cc \
    dsl_details.cc \
    wstring.cc \
    wstring_qt.cc \
    iconv.cc \
    folding.cc \
    ufile.cc \
    utf8.cc \
    dsl.cc \
    dictzip.c \
    langcoder.cc \
    fsencoding.cc \
    filetype.cc \
    audiolink.cc \
    language.cc \
    htmlescape.cc

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

DEFINES += UNICODE
DEFINES += _UNICODE

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

HEADERS += \
    dsl_details.hh \
    wstring.hh \
    wstring_qt.hh \
    ex.hh \
    iconv.hh \
    folding.hh \
    inc_case_folding.hh \
    inc_diacritic_folding.hh \
    gddebug.hh \
    ufile.hh \
    utf8.hh \
    dsl.hh \
    dictzip.h \
    langcoder.hh \
    htmlescape.hh \
    filetype.hh \
    audiolink.hh \
    qt4x5.hh \
    language.hh \
    fsencoding.hh
