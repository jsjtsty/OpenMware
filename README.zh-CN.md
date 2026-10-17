# OpenMware

[English](README.md)

一个用 C++20 编写的开源引擎复刻，对应一系列日式视觉小说所用的 “Mware” 引擎。它直接运行游戏原版数据和 Squirrel 脚本：只需要你合法持有的游戏本体。

当前目标是 Steam 中文版《君与彼女与彼女之恋》。

**本仓库不包含任何游戏数据和素材。** 游戏本体需要你自行提供。所支持游戏的 NPK 档案密钥放在 `keys.local.txt` 中（当前为私有仓库；公开仓库前请先移除该文件）。

## 当前状态

开发中。引擎已能启动游戏，并运行标题、序章以及二周目的“黑化”路线（包括其独立的 UI），支持文字、立绘、转场、特效、声音和 WMV 过场视频。

已知缺口：

* 手机页面（序章之后的选择界面）尚未实现，主线剧情因此无法继续。
* 音频只在无设备环境下检查过，没有实际听过。
* 只有 Windows（Direct3D 12）在真机上跑过。Vulkan/Metal 着色器，以及 Linux、macOS、Android、iOS 构建，目前只做到能编译（或已配置 CI），运行时未测试。

## 特性

* NPK3 档案读取（AES-256 + zstd）、Squirrel 2.2.4 虚拟机（支持 32/64 位字节码），并重新实现了游戏的原生 API
* 基于 SDL3 GPU 的渲染器（Vulkan、Direct3D 12、Metal），HLSL 着色器以 SPIR-V、DXIL、MSL 三种格式预编译随仓库提供
* 文字排版与标记、贝塞尔路径、模糊/调色/扭曲特效，通过精简版 FFmpeg 解码 WMV3/VC-1 视频
* 按字节预算管理的纹理与音频缓存；支持游戏内“重启进入另一条路线”

## 构建

所有依赖（SDL3、zstd、libwebp、stb、FFmpeg）都由 CMake 拉取源码并编译，不需要包管理器。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

各平台说明（Windows 下编译 FFmpeg 需要 MSYS2、Android NDK、Xcode）以及着色器的重新生成方法见 [docs/building.md](docs/building.md)。

## 运行

```powershell
omw.exe "D:\Games\<游戏目录>" --keys keys.txt
```

详见 [docs/running.md](docs/running.md)；手机端见 [docs/mobile.md](docs/mobile.md)。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `src/`、`apps/` | 引擎库与 `omw` 可执行文件 |
| `third_party/squirrel2` | Squirrel 2.2.4 分支 |
| `shaders/` | HLSL 源码与预编译的 `prebuilt/{spv,dxil,msl}` |
| `tools/`、`scripts/` | 开发工具（档案解包、脚本反汇编、逆向辅助脚本） |
| `docs/` | 引擎、格式与脚本笔记 |
| `.github/workflows` | Windows、macOS、Android、iOS 的 CI |

## 声明

OpenMware 是独立项目，与原开发商、发行商无关，也未获得其认可。请勿用于你不拥有的数据。本仓库尚未选择开源许可证，在添加之前保留所有权利。
