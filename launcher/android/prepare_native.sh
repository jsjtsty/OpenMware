#!/usr/bin/env bash
# Builds the engine for Android with the NDK and fills the Gradle project with the native parts:
#   prepare_native.sh [BUILD_DIR]      (default: build/android in the repository root)
# Needs ANDROID_NDK_HOME (or ANDROID_NDK_LATEST_HOME), cmake and ninja. Then:  gradle -p launcher/android assembleDebug
# The Java classes of SDL come from the SDL source CMake fetched into BUILD_DIR, so they match libSDL3.so exactly.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/android}"
ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
[ -n "$ndk" ] || { echo "set ANDROID_NDK_HOME" >&2; exit 1; }
app="$root/launcher/android/app/src"

cmake -S "$root" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
cmake --build "$build" --target openmware

sdl_src="$build/_deps/sdl3-src"
sdl_lib="$(find "$build/_deps/sdl3-build" -maxdepth 1 -name 'libSDL3.so' | head -n1)"
[ -f "$build/libmain.so" ] || { echo "missing $build/libmain.so" >&2; exit 1; }
[ -n "$sdl_lib" ] || { echo "missing libSDL3.so under $build/_deps/sdl3-build" >&2; exit 1; }

rm -rf "$app/sdl-java" "$app/main/jniLibs" "$app/main/assets"
mkdir -p "$app/sdl-java/org/libsdl"
cp -R "$sdl_src/android-project/app/src/main/java/org/libsdl/app" "$app/sdl-java/org/libsdl/app"

mkdir -p "$app/main/jniLibs/arm64-v8a" "$app/main/assets/shaders" "$app/main/assets/licenses"
cp "$build/libmain.so" "$app/main/jniLibs/arm64-v8a/libmain.so"
cp "$sdl_lib" "$app/main/jniLibs/arm64-v8a/libSDL3.so"
# FFmpeg (LGPL) stays dynamic: libavcodec-<major>.so etc. (not the unversioned link names)
cp "$build"/ffmpeg-install/lib/lib*-[0-9]*.so "$app/main/jniLibs/arm64-v8a/"
cp "$build"/ffmpeg-install/licenses/* "$app/main/assets/licenses/"
cp "$root"/shaders/prebuilt/spv/* "$app/main/assets/shaders/"
echo "Android project ready: gradle -p launcher/android assembleDebug"
