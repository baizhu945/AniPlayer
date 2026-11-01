#!/usr/bin/env bash
# Builds with the system Qt moc and g++. qmake on this Qt install cannot see
# its module pri files, so the Makefile is generated here instead.
set -euo pipefail
cd "$(dirname "$0")"

qt_prefix="$(qmake6 -query QT_INSTALL_PREFIX)"
qt_libs="$(qmake6 -query QT_INSTALL_LIBS)"
moc="${qt_prefix}/libexec/moc"

system_lib="$(readlink -f /run/current-system/sw/lib/libmpv.so)"
mpv_include=""
mpv_libdir=""
for pc in /nix/store/*-mpv-*-dev/lib/pkgconfig/mpv.pc; do
  [[ -f "$pc" ]] || continue
  prefix="$(sed -n 's/^prefix=//p' "$pc")"
  includedir="$(sed -n 's/^includedir=//p' "$pc")"
  if [[ -e "$prefix/lib/libmpv.so" && "$(readlink -f "$prefix/lib/libmpv.so")" == "$system_lib" ]]; then
    mpv_include="$includedir"
    mpv_libdir="$prefix/lib"
    break
  fi
done

gl_include=""
for header in /nix/store/*libglvnd*-dev/include/GL/gl.h; do
  [[ -f "$header" ]] || continue
  gl_include="$(dirname "$(dirname "$header")")"
  break
done

if [[ -z "$mpv_include" || -z "$mpv_libdir" || -z "$gl_include" || ! -x "$moc" ]]; then
  echo "找不到 libmpv 头文件、OpenGL 头文件或 Qt moc" >&2
  exit 1
fi

mkdir -p build/moc
includes=(
  -I"$qt_prefix/include"
  -I"$qt_prefix/include/QtCore"
  -I"$qt_prefix/include/QtGui"
  -I"$qt_prefix/include/QtWidgets"
  -I"$qt_prefix/include/QtOpenGL"
  -I"$qt_prefix/include/QtOpenGLWidgets"
  -I"$mpv_include"
  -I"$gl_include"
  -Isrc
)

declare -A moc_headers=(
  [moc_i18n.cpp]=src/i18n.h
  [moc_mediainfo.cpp]=src/mediainfo.h
  [moc_shortcuts.cpp]=src/shortcuts.h
  [moc_mpvwidget.cpp]=src/mpvwidget.h
  [moc_mainwindow.cpp]=src/mainwindow.h
)

for output in "${!moc_headers[@]}"; do
  "$moc" "${includes[@]}" "${moc_headers[$output]}" -o "build/moc/$output"
done

sources=(
  src/main.cpp
  src/i18n.cpp
  src/shaders.cpp
  src/shortcuts.cpp
  src/mediainfo.cpp
  src/mpvwidget.cpp
  src/mainwindow.cpp
  src/selftest.cpp
  build/moc/moc_i18n.cpp
  build/moc/moc_mediainfo.cpp
  build/moc/moc_shortcuts.cpp
  build/moc/moc_mpvwidget.cpp
  build/moc/moc_mainwindow.cpp
)

cxxflags=(
  -std=c++17
  -fPIC
  -Wall
  -Wextra
  -Wno-unused-parameter
  -finput-charset=UTF-8
  "${includes[@]}"
)

objects=()
for source in "${sources[@]}"; do
  object="build/${source%.cpp}.o"
  mkdir -p "$(dirname "$object")"
  g++ "${cxxflags[@]}" -c "$source" -o "$object"
  objects+=("$object")
done

g++ "${objects[@]}" \
  -Wl,--disable-new-dtags \
  -Wl,-rpath,"$qt_libs" \
  -Wl,-rpath,"$mpv_libdir" \
  -L"$qt_libs" \
  -L"$mpv_libdir" \
  -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets -lQt6Gui -lQt6Core -lmpv \
  -o build/aniplayer
