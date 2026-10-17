#!/usr/bin/env bash
# Fills the Android project with everything that is not checked in:
#   prepare_project.sh BUILD_DIR
#     BUILD_DIR  the CMake build tree of an Android build (holds libmain.so and, from SDL's source fetched into
#                _deps/sdl3-src, the Java classes and libSDL3.so that match each other)
# The SPIR-V shaders come from shaders/prebuilt/spv.
set -euo pipefail

build="$1"
root="$(cd "$(dirname "$0")/../.." && pwd)"
app="$root/android/app/src"

sdl_src="$build/_deps/sdl3-src"
sdl_lib="$(find "$build/_deps/sdl3-build" -maxdepth 1 -name 'libSDL3.so' | head -n1)"
[ -f "$build/libmain.so" ] || { echo "missing $build/libmain.so" >&2; exit 1; }
[ -n "$sdl_lib" ] || { echo "missing libSDL3.so under $build/_deps/sdl3-build" >&2; exit 1; }

rm -rf "$app/sdl-java"
mkdir -p "$app/sdl-java/org/libsdl"
cp -R "$sdl_src/android-project/app/src/main/java/org/libsdl/app" "$app/sdl-java/org/libsdl/app"

mkdir -p "$app/main/jniLibs/arm64-v8a" "$app/main/assets/shaders"
cp "$build/libmain.so" "$app/main/jniLibs/arm64-v8a/libmain.so"
cp "$sdl_lib" "$app/main/jniLibs/arm64-v8a/libSDL3.so"
cp "$root"/shaders/prebuilt/spv/* "$app/main/assets/shaders/"
echo "Android project ready"
