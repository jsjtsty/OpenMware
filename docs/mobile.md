# Android and iOS

The app contains no game data. Put your own copy next to the keys file in the app's data folder:

```
<data folder>/game/        the game's files (the .npk archives)
<data folder>/keys.txt     the decryption keys
<data folder>/save/        written by the game
```

* **Android**: `Android/data/org.openmware.omw/files/` on the device's shared storage. Copy with `adb push game /sdcard/Android/data/org.openmware.omw/files/game` (Android 11+ file managers cannot open `Android/data`; use adb or a computer).
* **iOS**: the app's Documents folder, visible in the Files app under "On My iPhone > OpenMware" (file sharing is enabled in the Info.plist). Copy with Finder / the Files app.

Both builds start the game from `game/` automatically when no path is given. Touch: tap = left click, long press / two-finger tap is not mapped yet (the right-click menu needs a keyboard or mouse for now).

Status: the Windows build is the only one that has been run. The Android/iOS workflows compile the same code, but Vulkan and Metal rendering, touch input and these folder conventions are untested on devices.

The iOS package is unsigned: re-sign it (AltStore, Sideloadly, or your own certificate) before installing.
