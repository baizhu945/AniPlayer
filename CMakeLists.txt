cmake_minimum_required(VERSION 3.19)
project(AniPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_executable(aniplayer
  src/main.cpp
  src/i18n.cpp
  src/shaders.cpp
  src/shortcuts.cpp
  src/mediainfo.cpp
  src/mpvwidget.cpp
  src/mainwindow.cpp
  src/selftest.cpp
  src/i18n.h
  src/shaders.h
  src/shortcuts.h
  src/mediainfo.h
  src/mpvwidget.h
  src/mainwindow.h
  src/selftest.h
)

target_include_directories(aniplayer PRIVATE src)
target_compile_options(aniplayer PRIVATE -Wall -Wextra -Wno-unused-parameter -finput-charset=UTF-8)
target_link_libraries(aniplayer PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  PkgConfig::MPV
)

include(GNUInstallDirs)
install(TARGETS aniplayer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
  DIRECTORY shaders/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/aniplayer/shaders"
  FILES_MATCHING
    PATTERN "*.glsl"
    PATTERN "LICENSE"
)
install(
  FILES icons/aniplayer.png
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps"
)
install(
  FILES share/applications/aniplayer.desktop
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
