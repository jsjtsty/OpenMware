# Android and iOS

The apps contain no game data. Open OpenMware, tap **Add game**, pick the folder with your copy of the game (the one holding `script.npk`, `cg.npk`, ...) and import the keys file (or paste the keys). See [launchers.md](launchers.md) for what the apps do.

* **Android**: the archives are copied into the app's private storage (the system folder picker hands out `content://` access, not paths), so you need the free space once. The original folder can be deleted afterwards. Save data is exported from the game's page (a zip you can share).
* **iOS**: the folder can stay where it is (the app keeps a security-scoped bookmark; files in iCloud Drive must be downloaded first) or be copied into the app. Pick it from the Files app: *On My iPhone*, iCloud Drive, a USB drive or a network share all work.

The command-line engine (`OpenMware GAME_DIR --keys FILE --data-dir DIR`) is what the Android activity and the macOS app run underneath; on iOS the same code runs in the app's process through libmware.

Status: the Android and iOS apps have only been built by CI, not run on devices. The iOS package is unsigned: re-sign it (AltStore, Sideloadly, or your own certificate) before installing; the Android APK is debug-signed.
