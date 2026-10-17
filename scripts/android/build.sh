#!/usr/bin/env bash
# Builds the Android app end to end:  scripts/android/build.sh [debug|release]
# prepare_native.sh builds libmain.so / libSDL3.so / FFmpeg with the NDK and fills the Gradle project, Gradle packs the APK.
# Needs: ANDROID_NDK_HOME, the Android SDK (ANDROID_HOME), JDK 17, cmake, ninja, gradle 8.10+.
set -euo pipefail

variant="${1:-debug}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
bash "$root/launcher/android/prepare_native.sh"
case "$variant" in
  debug) task=assembleDebug; apk=debug/app-debug.apk ;;
  release) task=assembleRelease; apk=release/app-release.apk ;;
  *) echo "usage: build.sh [debug|release]" >&2; exit 2 ;;
esac
gradle --no-daemon -p "$root/launcher/android" "$task"
mkdir -p "$root/dist"
cp "$root/launcher/android/app/build/outputs/apk/$apk" "$root/dist/OpenMware-android-arm64-$variant.apk"
echo "wrote dist/OpenMware-android-arm64-$variant.apk"
