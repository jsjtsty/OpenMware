#!/usr/bin/env bash
# Packages a Linux build (configured with -DOMW_BUILD_LAUNCHER=ON) into dist/OpenMware-linux-x64.tar.gz:
#   scripts/linux/package.sh [build-dir]
# The launcher uses the distribution's Qt 6 (LGPL, shared libraries): install qt6-base (Debian/Ubuntu: libqt6widgets6).
# FFmpeg (LGPL) stays a set of shared libraries in lib/ next to the player.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build}"
out="$root/dist/OpenMware-linux-x64"

rm -rf "$out"
mkdir -p "$out/lib" "$out/licenses"
cp "$build/openmware" "$build/openmware-player" "$out/"
cp -a "$build"/lib/lib*.so* "$out/lib/"
cp -r "$build/shaders" "$out/shaders"
cp "$build"/ffmpeg-install/licenses/* "$out/licenses/"
cp "$root/docs/third-party.md" "$out/licenses/THIRD-PARTY.md"
cp "$root/launcher/qt/dist/QT-NOTICE.txt" "$out/licenses/"
cp "$root/launcher/qt/dist/openmware.desktop" "$out/"
cp "$root/launcher/icon-512.png" "$out/openmware.png"
cp "$root/launcher/qt/dist/README-linux.txt" "$out/README.txt"
strip --strip-unneeded "$out/openmware" "$out/openmware-player" || true
tar -C "$root/dist" -czf "$root/dist/OpenMware-linux-x64.tar.gz" OpenMware-linux-x64
echo "wrote $root/dist/OpenMware-linux-x64.tar.gz"
