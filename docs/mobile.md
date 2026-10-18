# Android and iOS

The apps contain no game data. On Android, open OpenMware, tap **Add game**, pick the folder with your copy of the game (the one holding `script.npk`, `cg.npk`, ...) and import the keys file (or paste the keys). See [launchers.md](launchers.md) for what the apps do.

* **Android**: the archives are copied into the app's private storage (the system folder picker hands out `content://` access, not paths), so you need the free space once. The original folder can be deleted afterwards. Save data is exported from the game's page (a zip you can share).
* **iOS**: there is no Add button. With the Files app, put the game folder into *On My iPad/iPhone › OpenMware* (the app's Documents folder); the library lists every folder there that holds `script.npk`, and drops it again when the folder is gone (its saves are kept and come back with a folder of the same name). Keys are pasted on the game's page; a `keys.txt` inside the game folder, or one in the OpenMware folder for all games, is read automatically. The system folder picker is not used because with some re-signing tools it never hands the chosen folder to the app.

The command-line engine (`OpenMware GAME_DIR --keys FILE --data-dir DIR`) is what the Android activity and the macOS app run underneath; on iOS the same code runs in the app's process through libmware.

Status: the Android and iOS apps have only been built by CI, not run on devices. The iOS package is unsigned: re-sign it (AltStore, Sideloadly, or your own certificate) before installing; the Android APK is debug-signed.

## Controls on a touch screen

The game's own scripts already contain the phone controls; the engine feeds them the touch state (`getTouchState`, `getTouchTime`, `isTouchMoving`, ...):

* **Tap**: click (SDL turns a tap into a left click): advance the text, press buttons, pick a choice.
* **Press and hold (about 0.5 s, finger not moving)**: open the system menu (the right-click menu on a PC); do the same to close it. The menu has skip, auto, backlog, save/load and config.
* **Two-finger tap** (a second finger touching while one is down): right click, which opens the system menu at once and closes it again.
* **Android back button**: Esc (asks whether to quit).

Test without a device: `openmware-player ... --touch FRAME:X:Y:FRAMES` (repeatable, each one is its own finger) holds a finger at game position (X, Y) for FRAMES frames.
