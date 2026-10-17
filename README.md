# OpenMware

[简体中文](README.zh-CN.md)

An open-source reimplementation of the engine behind a family of Japanese visual novels (the "Mware" engine), written in C++20. It runs the original game data and Squirrel scripts: the engine only needs a legitimately owned copy of the game.

The current target is the Steam Chinese release of *君与彼女与彼女之恋* (*You and Me and Her*).

**This repository contains no game data and no assets.** You supply your own copy of the game. The NPK archive keys of the supported game are in `keys.local.txt` (private repository; remove the file before making it public).

## Status

Work in progress. The engine boots the game, plays the title, the prologue and the second-playthrough "dark" route (including its separate UI), with text, sprites, transitions, effects, sound and WMV cutscenes.

Known gaps:

* The phone page (the choice screen after the prologue) is not implemented, which blocks the main story.
* Audio has only been checked headless; it has never been listened to.
* Only Windows (Direct3D 12) has been run on real hardware. The Vulkan/Metal shaders, Linux, macOS, Android and iOS builds compile (or are set up in CI) but are untested at runtime.

## Features

* NPK3 archive reader (AES-256 + zstd), Squirrel 2.2.4 VM (32/64-bit bytecode) with the game's native API reimplemented
* SDL3 GPU renderer (Vulkan, Direct3D 12, Metal) with HLSL shaders shipped pre-compiled as SPIR-V, DXIL and MSL
* Text layout and markup, bezier paths, blur/colour/warp effects, WMV3/VC-1 video via a minimal FFmpeg
* Byte-budgeted caches for textures and decoded sound; reboot-into-another-route support

## Building

All dependencies (SDL3, zstd, libwebp, stb, FFmpeg) are fetched as source and compiled by CMake; no package manager is needed.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

See [docs/building.md](docs/building.md) for per-platform notes (MSYS2 for FFmpeg on Windows, Android NDK, Xcode) and for regenerating the shaders.

## Running

```powershell
OpenMware.exe "D:\Games\<game folder>" --keys keys.txt
```

See [docs/running.md](docs/running.md) and, for phones, [docs/mobile.md](docs/mobile.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `src/`, `apps/` | engine libraries and the `omw` executable |
| `third_party/squirrel2` | Squirrel 2.2.4 fork |
| `shaders/` | HLSL sources and pre-compiled `prebuilt/{spv,dxil,msl}` |
| `tools/`, `scripts/` | developer tools (archive extractor, script disassembler, reverse-engineering helpers) |
| `docs/` | engine, format and script notes |
| `.github/workflows` | CI for Windows, macOS, Android, iOS |

## Legal

OpenMware is an independent project, not affiliated with or endorsed by the original developers or publishers. Do not use it with data you do not own. No license has been chosen for this repository yet; until one is added, all rights are reserved.
