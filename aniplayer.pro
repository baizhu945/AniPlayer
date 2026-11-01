QT += core gui widgets openglwidgets
# This Qt build lists clang_module_maps as disabled in qmodule.pri, but that
# file is not loaded by qmake, so qtConfig() aborts. Declare it disabled here.
QT.global_private.disabled_features += clang_module_maps swift_interop
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = aniplayer

QMAKE_CXXFLAGS += -Wall -Wextra -Wno-unused-parameter -finput-charset=UTF-8

isEmpty(MPV_INCLUDE): error("MPV_INCLUDE is empty. Run ./build.sh")
isEmpty(MPV_LIBDIR): error("MPV_LIBDIR is empty. Run ./build.sh")

INCLUDEPATH += $$MPV_INCLUDE
LIBS += -L$$MPV_LIBDIR -lmpv
QMAKE_RPATHDIR += $$MPV_LIBDIR $$[QT_INSTALL_LIBS]
QMAKE_LFLAGS += -Wl,--disable-new-dtags

SOURCES += \
    src/main.cpp \
    src/i18n.cpp \
    src/shaders.cpp \
    src/shortcuts.cpp \
    src/mediainfo.cpp \
    src/mpvwidget.cpp \
    src/mainwindow.cpp \
    src/selftest.cpp

HEADERS += \
    src/i18n.h \
    src/shaders.h \
    src/shortcuts.h \
    src/mediainfo.h \
    src/mpvwidget.h \
    src/mainwindow.h \
    src/selftest.h
