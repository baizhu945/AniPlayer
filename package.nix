# Package expression for Nixpkgs and Home Manager.
#
# Home Manager, without editing NixOS system configuration:
#
#   home.packages = [
#     (pkgs.callPackage /path/to/AniPlayer/package.nix { })
#   ];
#
# Nixpkgs: copy this file to pkgs/by-name/an/aniplayer/package.nix and replace
# `src` with fetchFromGitHub. Also set meta.maintainers, and point meta.license
# at the player's own license. The Anime4K shaders are MIT and Unlicense; the
# player sources do not have a LICENSE file yet.
{
  lib,
  stdenv,
  cmake,
  desktop-file-utils,
  ffmpeg-headless,
  imagemagick,
  libglvnd,
  mpv,
  pkg-config,
  qt6,
}:

stdenv.mkDerivation {
  pname = "aniplayer";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      ./CMakeLists.txt
      ./README.md
      ./icons
      ./shaders
      ./share
      ./src
    ];
  };

  nativeBuildInputs = [
    cmake
    desktop-file-utils
    imagemagick
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    ffmpeg-headless
    libglvnd
    mpv
    qt6.qtbase
    qt6.qtsvg
    qt6.qtwayland
  ];

  env.LANG = "C.UTF-8";

  # Smaller icons are generated from icons/aniplayer.png. AppImage tooling
  # reads the 256x256 hicolor icon as .DirIcon.
  postInstall = ''
    for size in 256 128 64; do
      install -d "$out/share/icons/hicolor/''${size}x''${size}/apps"
      magick "$out/share/icons/hicolor/1024x1024/apps/aniplayer.png" \
        -filter Lanczos -resize "''${size}x''${size}" \
        "$out/share/icons/hicolor/''${size}x''${size}/apps/aniplayer.png"
    done
    desktop-file-validate "$out/share/applications/aniplayer.desktop"
  '';

  preFixup = ''
    qtWrapperArgs+=(
      --prefix PATH : ${lib.makeBinPath [ ffmpeg-headless ]}
      --set ANIPLAYER_SHADER_DIR "$out/share/aniplayer/shaders"
    )
  '';

  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    test -f "$out/share/aniplayer/shaders/Anime4K_Clamp_Highlights.glsl"
    test -f "$out/share/icons/hicolor/1024x1024/apps/aniplayer.png"
    test -f "$out/share/icons/hicolor/256x256/apps/aniplayer.png"
    export QT_QPA_PLATFORM=offscreen
    "$out/bin/aniplayer" --help | grep -q AniPlayer
    runHook postInstallCheck
  '';

  meta = {
    description = "Local anime player with Anime4K super resolution";
    longDescription = ''
      AniPlayer plays local video with libmpv. Super resolution runs the same
      Anime4K efficiency and quality shader chains as Kazumi. Media details
      come from ffprobe.
    '';
    homepage = "https://github.com/baizhu945/AniPlayer";
    # Anime4K shaders are MIT and Unlicense. The player sources still need
    # their own LICENSE before this list can be treated as the package license.
    license = with lib.licenses; [
      mit
      unlicense
    ];
    mainProgram = "aniplayer";
    platforms = lib.platforms.linux;
  };
}
