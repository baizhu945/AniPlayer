# `nix-build` entry point. Uses the Nixpkgs on NIX_PATH, so Home Manager and
# this build share the same Qt, mpv, and ffmpeg as the rest of the system.
{ pkgs ? import <nixpkgs> { } }:
pkgs.callPackage ./package.nix { }
