#!/usr/bin/env bash
# Build a type-2 AppImage.
#
# Qt, libmpv, ffmpeg and ffprobe are bundled. OpenGL, EGL and Vulkan stay on
# the host so Anime4K can use the machine's GPU driver. The icon is
# icons/aniplayer.png.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${ANIPLAYER_APPIMAGE_TOOLS:-}" ]]; then
  if ! command -v patchelf >/dev/null || ! command -v mksquashfs >/dev/null; then
    exec nix-shell -p patchelf squashfsTools binutils --run "ANIPLAYER_APPIMAGE_TOOLS=1 bash '$0'"
  fi
fi

out="$(nix-build --no-out-link)"
bin="$out/bin/.aniplayer-wrapped"
ff_bin="$(strings "$out/bin/aniplayer" | sed -n 's#^\(/nix/store/[^ ]*ffmpeg-headless[^ ]*/bin\)$#\1#p' | head -n 1)"
if [[ ! -x "$bin" || -z "$ff_bin" || ! -x "$ff_bin/ffprobe" ]]; then
  echo "nix package is missing the player or ffprobe" >&2
  exit 1
fi

appdir="build/appdir"
if [[ -d "$appdir" ]]; then
  chmod -R u+w "$appdir"
fi
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/lib" "$appdir/usr/share/applications"

declare -A seen=()
declare -A sonames=()
declare -a queue=()

note_library() {
  local lookup="$1"
  [[ -n "$lookup" && -e "$lookup" ]] || return 0
  local name real
  name="$(basename "$lookup")"
  real="$(readlink -f "$lookup")"
  # Keep the ldd filename (usually the SONAME). realpath drops the .so.6 symlink.
  if [[ "$name" == lib*.so* && "$real" != *"/qt-6/plugins/"* ]]; then
    sonames["$name"]="$real"
  fi
  [[ -n "${seen[$real]:-}" ]] && return 0
  seen["$real"]=1
  queue+=("$real")
}

skip_lib() {
  # Keep libglvnd (libGL, libEGL, libGLX). It dispatches to the host driver.
  # Leave the driver itself (libcuda, libnvidia-*) on the machine.
  case "$(basename "$1")" in
    ld-linux-*.so.*|libcuda.so*|libnvidia-*)
      return 0
      ;;
  esac
  return 1
}

collect_ldd() {
  local file="$1"
  local line target
  while IFS= read -r line; do
    target=""
    if [[ "$line" =~ =\>\ ([^[:space:]]+) ]]; then
      target="${BASH_REMATCH[1]}"
    elif [[ "$line" =~ ^[[:space:]]*(/[^[:space:]]+) ]]; then
      target="${BASH_REMATCH[1]}"
    fi
    [[ -n "$target" && "$target" != "not" ]] || continue
    note_library "$target"
  done < <(ldd "$file" 2>/dev/null || true)
}

note_library "$bin"
note_library "$ff_bin/ffprobe"
note_library "$ff_bin/ffmpeg"

mapfile -t plugin_roots < <(strings "$out/bin/aniplayer" | sed -n 's#^\(/nix/store/[^ ]*/lib/qt-6/plugins\)$#\1#p' | sort -u)
plugin_kinds=(
  platforms
  platforminputcontexts
  platformthemes
  xcbglintegrations
  imageformats
  iconengines
  egldeviceintegrations
  generic
  tls
  wayland-decoration-client
  wayland-graphics-integration-client
  wayland-shell-integration
)

for root in "${plugin_roots[@]}"; do
  # Skip the qml tooling package; this player does not use QML.
  [[ "$root" == *qtdeclarative* ]] && continue
  for kind in "${plugin_kinds[@]}"; do
    [[ -d "$root/$kind" ]] || continue
    mkdir -p "$appdir/usr/lib/qt-6/plugins/$kind"
    while IFS= read -r plugin; do
      install -m 755 "$plugin" "$appdir/usr/lib/qt-6/plugins/$kind/$(basename "$plugin")"
      note_library "$plugin"
    done < <(find "$root/$kind" -maxdepth 1 \( -type f -o -type l \) -name '*.so')
  done
done

interpreter="$(readelf -l "$bin" | sed -n 's/.*Requesting program interpreter: \(.*\)\]/\1/p')"
if [[ ! -e "$interpreter" ]]; then
  echo "could not find the dynamic linker" >&2
  exit 1
fi

index=0
while (( index < ${#queue[@]} )); do
  current="${queue[$index]}"
  index=$((index + 1))
  [[ "$(head -c 4 "$current" 2>/dev/null || true)" == $'\x7fELF' ]] || continue
  if skip_lib "$current"; then
    continue
  fi
  collect_ldd "$current"
done

for name in "${!sonames[@]}"; do
  if skip_lib "$name"; then
    continue
  fi
  install -m 755 "${sonames[$name]}" "$appdir/usr/lib/$name"
done

install -m 755 "$interpreter" "$appdir/usr/lib/$(basename "$interpreter")"
glibc_dir="$(dirname "$interpreter")"
shopt -s nullglob
for extra in "$glibc_dir"/libnss_*.so* "$glibc_dir"/libresolv.so*; do
  install -m 755 "$extra" "$appdir/usr/lib/$(basename "$extra")"
done
shopt -u nullglob

install -m 755 "$bin" "$appdir/usr/bin/aniplayer"
install -m 755 "$ff_bin/ffprobe" "$appdir/usr/bin/ffprobe"
install -m 755 "$ff_bin/ffmpeg" "$appdir/usr/bin/ffmpeg"
cp -a "$out/share/aniplayer" "$appdir/usr/share/aniplayer"
cp -a "$out/share/icons" "$appdir/usr/share/icons"
cp -a "$out/share/applications/aniplayer.desktop" "$appdir/usr/share/applications/aniplayer.desktop"
cp -a "$out/share/applications/aniplayer.desktop" "$appdir/aniplayer.desktop"
cp -a "$out/share/icons/hicolor/1024x1024/apps/aniplayer.png" "$appdir/aniplayer.png"
cp -a "$out/share/icons/hicolor/256x256/apps/aniplayer.png" "$appdir/.DirIcon"

patch_rpath() {
  local file="$1"
  local rpath="$2"
  [[ "$(head -c 4 "$file")" == $'\x7fELF' ]] || return 0
  case "$(basename "$file")" in
    ld-linux-*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libnss_*|libutil.so*|libanl.so*)
      return 0
      ;;
  esac
  chmod u+w "$file"
  patchelf --set-rpath "$rpath" "$file"
}

patch_rpath "$appdir/usr/bin/aniplayer" '$ORIGIN/../lib'
patch_rpath "$appdir/usr/bin/ffprobe" '$ORIGIN/../lib'
patch_rpath "$appdir/usr/bin/ffmpeg" '$ORIGIN/../lib'
while IFS= read -r lib; do
  patch_rpath "$lib" '$ORIGIN'
done < <(find "$appdir/usr/lib" -maxdepth 1 -type f)
while IFS= read -r plugin; do
  patch_rpath "$plugin" '$ORIGIN:$ORIGIN/..:$ORIGIN/../..:$ORIGIN/../../..'
done < <(find "$appdir/usr/lib/qt-6" -type f -name '*.so')

cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
set -eu
HERE=$(dirname "$(readlink -f "$0")")
export PATH="$HERE/usr/bin${PATH:+:$PATH}"
export ANIPLAYER_SHADER_DIR="$HERE/usr/share/aniplayer/shaders"
export XDG_DATA_DIRS="$HERE/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export QT_PLUGIN_PATH="$HERE/usr/lib/qt-6/plugins"

gl_paths=""
for dir in \
  /run/opengl-driver/lib \
  /run/opengl-driver-32/lib \
  /usr/lib/x86_64-linux-gnu \
  /usr/lib64 \
  /usr/lib
do
  if [ -d "$dir" ]; then
    gl_paths="${gl_paths:+$gl_paths:}$dir"
  fi
done
if [ -d /run/opengl-driver/lib/dri ]; then
  export LIBGL_DRIVERS_PATH="${LIBGL_DRIVERS_PATH:-/run/opengl-driver/lib/dri}"
  export LIBVA_DRIVERS_PATH="${LIBVA_DRIVERS_PATH:-/run/opengl-driver/lib/dri}"
fi

linker=$(echo "$HERE"/usr/lib/ld-linux-*.so.*)
exec "$linker" --library-path "$HERE/usr/lib${gl_paths:+:$gl_paths}" \
  "$HERE/usr/bin/aniplayer" "$@"
EOF
chmod 755 "$appdir/AppRun"

if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
  "$appdir/AppRun" --test
fi

runtime="build/appimage-runtime"
if [[ ! -x "$runtime" ]]; then
  found="$(find /nix/store -maxdepth 1 -name '*-appimage-type2-runtime-static-x86_64-*' ! -name '*.drv' -type f | head -n 1 || true)"
  if [[ -n "$found" ]]; then
    cp -L "$found" "$runtime"
  else
    curl -fsSL -o "$runtime" \
      https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
  fi
  chmod 755 "$runtime"
fi

mkdir -p dist
image="dist/AniPlayer-x86_64.AppImage"
rm -f "$image"
offset="$(stat -c %s "$runtime")"
rm -f "$image"
mksquashfs "$appdir" "$image" \
  -offset "$offset" \
  -all-root \
  -noappend \
  -no-progress \
  >/dev/null
# mksquashfs leaves a hole where the runtime belongs.
dd if="$runtime" of="$image" conv=notrunc status=none
chmod 755 "$image"
echo "$image"
