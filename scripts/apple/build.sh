#!/usr/bin/env bash
# Builds the Apple launchers end to end:
#   scripts/apple/build.sh macos|ios
# 1. CMake builds libmware (mware.framework), FFmpeg's dylibs, the shaders (.metallib) and, on macOS, the player.
# 2. The results are staged in launcher/apple/Staging/<platform>/.
# 3. XcodeGen generates the Xcode project and xcodebuild builds the SwiftUI app around them.
# 4. dist/ gets OpenMware-macos-arm64.zip (ad-hoc signed) or OpenMware-ios-arm64-unsigned.ipa.
# Needs: Xcode (with the Metal toolchain), cmake, ninja (macOS), xcodegen (brew install xcodegen).
set -euo pipefail

platform="${1:?usage: build.sh macos|ios}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
apple="$root/launcher/apple"
build="$root/build/apple-$platform"
stage="$apple/Staging/$platform"
derived="$root/build/apple-$platform-xcode"
dist="$root/dist"

case "$platform" in
  macos)
    cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
    cmake --build "$build" --target mware openmware
    framework="$build/mware.framework"
    ;;
  ios)
    cmake -S "$root" -B "$build" -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
    cmake --build "$build" --config Release --target mware -- \
      CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=""
    framework="$build/Release-iphoneos/mware.framework"
    ;;
  *) echo "unknown platform $platform" >&2; exit 2 ;;
esac

rm -rf "$stage"
mkdir -p "$stage/ffmpeg" "$stage/player" "$stage/shaders" "$stage/licenses"
cp -R "$framework" "$stage/mware.framework"
# FFmpeg's dylibs, by the names they are loaded under (libavcodec.63.dylib, not the unversioned or fully versioned links).
find -E "$build/ffmpeg-install/lib" -maxdepth 1 -regex '.*/lib[a-z]+\.[0-9]+\.dylib' -exec cp -L {} "$stage/ffmpeg/" \;
cp -R "$build/shaders/." "$stage/shaders/"
cp "$build"/ffmpeg-install/licenses/* "$stage/licenses/"
cp "$root/docs/third-party.md" "$stage/licenses/THIRD-PARTY.md"
if [ "$platform" = macos ]; then cp "$build/openmware-player" "$stage/player/"; fi

( cd "$apple" && xcodegen generate )

if [ "$platform" = macos ]; then
  xcodebuild -project "$apple/OpenMware.xcodeproj" -scheme OpenMware-macOS -configuration Release \
    -destination 'platform=macOS' -derivedDataPath "$derived" \
    CODE_SIGNING_ALLOWED=NO ARCHS=arm64 build
  app="$derived/Build/Products/Release/OpenMware.app"
  codesign --force --deep --sign - "$app"
  mkdir -p "$dist"
  ditto -c -k --keepParent "$app" "$dist/OpenMware-macos-arm64.zip"
  echo "wrote $dist/OpenMware-macos-arm64.zip"
else
  xcodebuild -project "$apple/OpenMware.xcodeproj" -scheme OpenMware-iOS -configuration Release \
    -destination 'generic/platform=iOS' -derivedDataPath "$derived" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" ARCHS=arm64 build
  app="$derived/Build/Products/Release-iphoneos/OpenMware.app"
  rm -rf "$dist/Payload"
  mkdir -p "$dist/Payload"
  cp -R "$app" "$dist/Payload/"
  ( cd "$dist" && rm -f OpenMware-ios-arm64-unsigned.ipa && zip -qry OpenMware-ios-arm64-unsigned.ipa Payload && rm -rf Payload )
  echo "wrote $dist/OpenMware-ios-arm64-unsigned.ipa"
fi
