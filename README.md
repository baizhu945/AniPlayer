# AniPlayer

![AniPlayer](icons/aniplayer.png)

AniPlayer 是一个本地播放器，用来在自己的电脑上播放动漫，并用 GPU 对画面做超分辨率。

播放引擎是 libmpv。超分辨率直接跑 Anime4K 的 GLSL 着色器，效率档和质量档与 Kazumi 使用的预设一致。媒体信息来自 ffprobe。界面可以在中文和 English 之间切换。

## 可以做什么

- 打开本地视频、音频，或一个直接媒体地址
- 对动漫画面做超分辨率：关闭、效率档、质量档
- 播放列表、倍速、循环、音轨和字幕
- 从顶栏或右键菜单查看视频和音频详情
- 在界面里改快捷键，或直接编辑 `~/.config/AniPlayer/shortcuts.conf`
- 再次打开同一个视频时，默认从上次退出的位置继续。播完后下次从头开始。可在设置里关掉

效率档是 Kazumi 的轻量链，质量档是 Anime4K Mode A（HQ）。质量档对显卡的要求更高。

## 构建和运行

需要本机已经有的 Qt 6、libmpv、OpenGL 和 ffprobe。构建脚本自己查找这些库，不用改 Nix 配置。

```bash
./build.sh
./build/aniplayer
./build/aniplayer /path/to/anime.mkv
```

着色器在 `shaders/`。程序从可执行文件所在目录向上查找这个文件夹，也会查找安装后的 `share/aniplayer/shaders`。

## Nix 和 Home Manager

`package.nix` 按 nixpkgs 的写法调用 CMake 安装程序、着色器、桌面项和图标。把它拷进 nixpkgs 的 `pkgs/by-name/an/aniplayer/package.nix` 之前，把 `src` 换成 `fetchFromGitHub`，并补上主页、维护者和播放器自身的许可证。着色器是 Anime4K 的 MIT / Unlicense。

不改系统 Nix 配置，也可以在项目目录里构建：

```bash
nix-build
./result/bin/aniplayer
```

Home Manager 只在自己的 `home.packages` 里加一行，用当前的 `pkgs`，这样 Qt 和 mpv 跟系统是同一套：

```nix
(pkgs.callPackage /path/to/AniPlayer/package.nix { })
```

## AppImage

发布包在 [Releases](https://github.com/baizhu945/AniPlayer/releases)。下载 `AniPlayer-x86_64.AppImage` 后直接运行。

```bash
./packaging/build-appimage.sh
./dist/AniPlayer-x86_64.AppImage
```

启动器图标来自 `icons/aniplayer.png`。1024×1024 原图放在 hicolor 和 AppImage 根目录，256×256 用作 `.DirIcon`。Qt、libmpv 和 ffprobe 打进包里。OpenGL 仍用本机驱动，这样 Anime4K 可以走显卡。在 NixOS 上驱动目录是 `/run/opengl-driver`。

## 致谢

- [Kazumi](https://github.com/Predidit/Kazumi)：效率档和质量档的着色器组合遵循 Kazumi 的 Anime4K 预设。
- [Anime4K](https://github.com/bloc97/Anime4K)：超分辨率着色器来自 Anime4K。Anime4K © bloc97，MIT / Unlicense。
