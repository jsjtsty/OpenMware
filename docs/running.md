# Running OpenMware

OpenMware is an engine; it does not contain the game. Point it at your own copy of the game's folder (the one holding the `.npk` archives):

```powershell
OpenMware.exe "D:\Games\<game folder>" --keys keys.txt
```

* `--keys <file>`: the decryption keys file (default `keys.local.txt`).
* `--data-dir <dir>`: where `save/` and `temp/` are written (default: the current directory; on mobile the app's own storage).
* `--shaders <dir>`: shader folder (default: `shaders` next to the executable).
* Windows tries Direct3D 12 first and falls back to Vulkan.

Left mouse / Enter advances, right mouse opens the menu, Ctrl skips read text.

See `docs/mobile.md` for Android and iOS.
