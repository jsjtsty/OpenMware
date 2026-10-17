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

* Windows: `avcodec-*.dll` etc. next to `omw.exe`
* Linux: `lib/` next to the executable (rpath `$ORIGIN/lib`)
* macOS / iOS: `Frameworks/` inside the app bundle (rpath `@executable_path/../Frameworks`)
* Android: `lib/arm64-v8a/lib*-<major>.so` inside the APK

The licence texts (`COPYING.LGPLv2.1`, `LICENSE.md`) are shipped in `licenses/` (`Contents/Resources/licenses` in the macOS bundle, `assets/licenses` in the APK). The exact source and configuration used are the pinned archive and flags in `cmake/FFmpeg.cmake`; anybody can rebuild the libraries from them.
