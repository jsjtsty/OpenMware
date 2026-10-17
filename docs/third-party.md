# Third-party software

| Component | License | How it is used |
| --- | --- | --- |
| SDL3 | zlib | statically linked (shared `libSDL3.so` on Android, as SDL's Java side requires) |
| zstd | BSD-3-Clause (or GPLv2, chosen: BSD) | static |
| libwebp | BSD-3-Clause | static, decoder only |
| stb | public domain / MIT | headers |
| Squirrel 2.2.4 | MIT | static, modified (`third_party/squirrel2`) |
| FFmpeg n9.0.2 | **LGPL-2.1-or-later** | **dynamic libraries**, see below |

## FFmpeg (LGPL)

FFmpeg is configured without `--enable-gpl` and `--enable-nonfree` (decoders: wmv3, vc1, wmapro, wmav2; demuxer: asf; plus swscale and swresample), and is built as shared libraries (`OMW_FFMPEG_SHARED=ON`, the default) that every package ships beside the program, so a user can replace them with their own build of FFmpeg:

* Windows: `avcodec-*.dll` etc. next to `OpenMware.exe` and `openmware-player.exe`
* Linux: `lib/` next to the executable (rpath `$ORIGIN/lib`)
* macOS / iOS: `Frameworks/` inside the app bundle (rpath `@executable_path/../Frameworks`)
* Android: `lib/arm64-v8a/lib*-<major>.so` inside the APK

The licence texts (`COPYING.LGPLv2.1`, `LICENSE.md`) are shipped in `licenses/` (`Contents/Resources/licenses` in the macOS bundle, `assets/licenses` in the APK). The exact source and configuration used are the pinned archive and flags in `cmake/FFmpeg.cmake`; anybody can rebuild the libraries from them.

## Qt (LGPL, Windows and Linux launcher only)

The Qt launcher (`launcher/qt`) uses Qt 6 Widgets, licensed under the **LGPL v3**. It is used unmodified and only as shared libraries: on Windows `windeployqt` puts `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll` and the plugins next to `OpenMware.exe`; the Linux package uses the distribution's `libQt6*.so`. `licenses/QT-NOTICE.txt` in every package holds the notice, the licence links and where to get the Qt source. The engine and the game itself (`openmware-player`) do not use Qt, and neither do the macOS, iOS and Android launchers.
