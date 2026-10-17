# Launchers

OpenMware is split into the engine and a native launcher per platform. The engine knows how to play one game; the launcher is the app the user sees: the game library, adding games, keys, saves, settings, and starting the engine.

```
include/mware/mware.h     libmware C API: mw_run (play), mw_probe (check archives + keys), mw_version
src/api/session.*         one run of the game: archives, window, renderer, audio, script loop
apps/openmware.cpp        command-line player (openmware-player; Android libmain.so); --probe checks archives and keys without a window
launcher/qt/              Qt 6 Widgets app for Windows and Linux (CMake option OMW_BUILD_LAUNCHER)
launcher/apple/           SwiftUI app for macOS and iOS (XcodeGen project.yml, Sources/Shared)
launcher/android/         Jetpack Compose app + SDL game activity (Gradle)
```

| Platform | Launcher | How the game starts |
| --- | --- | --- |
| macOS | SwiftUI | child process `openmware-player` inside the app bundle (crash isolation; the UI toolkit and SDL do not fight over the main thread) |
| iOS | SwiftUI | `mw_run` in the same process (iOS has no child processes); it returns when the game window closes |
| Android | Compose | `GameActivity` (an `SDLActivity`) gets the game folder in its intent; `libmain.so` is `apps/openmware.cpp` |
| Windows, Linux | Qt 6 Widgets | child process `openmware-player` next to the launcher (same reasons as macOS); the command-line player still works on its own |

## What the launchers do

* A library of games, each with a generated cover, status (ready / keys needed / incomplete), last played and play time.
* Add a game: pick the game folder, give it a name, import or paste the NPK keys. Apple can **link** the folder (a security-scoped bookmark, nothing duplicated) or **copy** it into the app; Android always copies the `.npk` files into app storage (the system folder picker returns `content://` URIs the engine cannot read directly).
* Per game: rename, replace keys, check files and keys (Apple: `mw_probe`), export save data as a zip, delete save data, remove the game.
* Every game has its own `data/` folder; the engine's working directory is that folder while playing, so scripts' `save/` and `temp/` never mix between games.
* English and Simplified Chinese (the Qt launcher follows the system language). Windows and Linux link the game folder in place or copy it into the library (`%APPDATA%/OpenMware`, `~/.local/share/OpenMware`), and check files and keys with `openmware-player --probe`.

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOMW_BUILD_LAUNCHER=ON && cmake --build build   # Windows/Linux: build/OpenMware(.exe) + openmware-player
scripts/linux/package.sh build   # dist/OpenMware-linux-x64.tar.gz
scripts/apple/build.sh macos     # dist/OpenMware-macos-arm64.zip   (needs Xcode, xcodegen, cmake, ninja)
scripts/apple/build.sh ios       # dist/OpenMware-ios-arm64-unsigned.ipa
scripts/android/build.sh debug   # dist/OpenMware-android-arm64-debug.apk   (needs the NDK, Android SDK, JDK 17, gradle)
```

The Apple script builds `mware.framework`, FFmpeg's dylibs and the Metal shaders with CMake, stages them in `launcher/apple/Staging/<platform>/`, generates the Xcode project with XcodeGen and builds the SwiftUI app around them. FFmpeg (LGPL) stays dynamic: its dylibs sit in the app's `Frameworks` folder. The Android script builds `libmain.so`, `libSDL3.so` and FFmpeg with the NDK and fills the Gradle project (`launcher/android/prepare_native.sh`).

## Status

Nothing of the launchers has run on a device yet: they are written against the platform SDKs and built by CI, but the Metal and Vulkan paths, the iOS in-process SDL window (SDL running inside a SwiftUI app), the macOS helper process and the Android activity hand-over are all untested.
