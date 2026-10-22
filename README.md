# Phone Mirror

独立实现的 Android → Windows 手机映射项目，目标是低延迟、低占用的日常投屏和控制。

**当前已接入 USB 实时预览代码，并保留采集文件本地播放。实时真机验证尚待完成。** 不包含或复用 scrcpy 的代码、服务端、可执行程序或内部协议。

## 当前已经实现

- C++20 协议库：40 字节大端头、类型长度上限、半包/粘包处理、异常流拒绝。
- C++ / Kotlin 共用协议测试向量，边界、截断、错误头和随机变异测试。
- Windows 原生播放器：打开采集文件、按时间戳播放、等比缩放、停止和重新播放。
- Media Foundation H.264 解码与 D3D11 视频处理；支持 GPU 表面输出及可选软件解码后上传显示。
- 离线协议检查工具 `pm-inspect`，以及实际解码渲染检查工具 `pm-playback-check`。
- Android 采集原型：用户授权后，通过 Surface 输入硬件 H.264 编码，最多采集 15 秒，写入应用私有目录的 `capture.pmstream`。
- GitHub Actions：Windows / Linux C++ 构建测试，以及 Android 单测、Lint、调试 APK 构建。工作流是否成功以仓库 Actions 实际结果为准。

- USB 实时预览：一次性会话认证、ADB 回环转发、有限发送队列和可取消接收。

**尚未实现**：远程手势/中文输入、音频、无线配对、拥塞后的关键帧恢复和完整录像。当前实时会话遇到积压会结束，需重新授权连接。

## 技术路线

```text
Android: Kotlin + MediaProjection + MediaCodec
                   ↓ 自研 PMIR 协议 + USB / ADB 回环转发
Windows: C++20 + Media Foundation + D3D11 / DXGI
```

手机需要安装伴侣 App 并授权。后续控制功能将单独请求无障碍服务权限；当前原型不申请控制权限。

## 构建桌面工程

需要 CMake ≥3.24 和支持 C++20 的编译器。Windows 推荐 Visual Studio 2022 C++ 工具链，亦可使用 MinGW-w64 + Ninja。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

MinGW-w64：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

不要在同一个构建目录混用不同生成器。Linux 仅构建协议库、测试和检查工具。

## 构建 Android 采集原型

需要 JDK 17、Android SDK Platform 35 和 Gradle **8.11.1**。AGP 固定为 8.9.2，Kotlin 固定为 2.1.20。设置 `ANDROID_HOME`，或在 `android/local.properties` 配置 `sdk.dir`（此文件不提交）。当前没有提交 Gradle Wrapper，须安装指定 Gradle，CI 已固定该版本。

```powershell
cd android
gradle --no-daemon testDebugUnitTest lintDebug assembleDebug
```

APK：`android/app/build/outputs/apk/debug/app-debug.apk`。也可从成功的 Actions 运行下载调试 APK。它只用于开发验证，不是已签名的正式发行版。

## USB 实时预览（开发版）

1. 安装本仓库最新调试 APK，连接 USB 并允许电脑调试。
2. 在手机点击“授权并开始 USB 实时投屏”，批准共享整个屏幕。
3. 两分钟内在电脑运行：

```powershell
./scripts/start-usb.ps1
# 无窗口、限时接收并输出帧数统计：
./scripts/start-usb.ps1 -CheckSeconds 20
```

脚本需要 PowerShell 7 和 ADB。默认使用 `build/` 或 `build/Release/` 下的程序；可通过 `-Adb`、`-Executable` 指定路径，多台手机时用 `-Serial` 选择。调试版通过 `run-as` 读取应用私有的一次性会话信息；认证令牌只经进程标准输入传递，不放在命令行或电脑文件中。

停止电脑接收、手机通知中的停止按钮或系统撤销共享都会结束会话。手机旋转也会结束预览，需重新授权。每次最长一小时；实时模式不保存画面，保留的旧采集文件不受影响。只有本机回环监听，没有局域网监听或无线连接。详见 [USB 会话设计](docs/usb-preview.md)。

## 手机采集验证

1. 安装调试 APK，打开 **Phone Mirror P0**。
2. 点击采集，选择完整屏幕并批准系统提示。
3. 等待 15 秒或主动停止，刷新状态查看实际编码器、尺寸、帧数。
4. 此次文件替换上次探测结果；上限 32 MiB。不要采集敏感内容。
5. 开启 USB 调试后，使用下述脚本导出调试应用私有文件并检查：

```powershell
./scripts/export-probe.ps1 -Serial YOUR_DEVICE_SERIAL
./build/pm-inspect.exe ./capture.pmstream
```

`run-as` 仅用于调试构建。导出脚本要求 PowerShell 7，以原始字节写文件，避免文本重定向破坏视频数据。`pm-inspect` 只检查消息边界；实际解码请使用下面的播放器或检查工具。

## 在电脑播放

打开 `PhoneMirror.exe`，点击“打开采集文件”，选择导出的 `.pmstream`。也可以直接指定文件：

```powershell
./build/PhoneMirror.exe ./capture.pmstream
./build/pm-playback-check.exe ./capture.pmstream
./build/pm-playback-check.exe ./capture.pmstream --software
```

Visual Studio 生成器的程序位于 `build/Release/`。若默认解码失败，勾选“兼容解码”后重新打开文件；兼容模式仍需要支持 NV12 视频处理的 D3D11 显卡，不是完全不依赖 GPU 的显示模式。

命令行检查器会报告输入帧数、解码帧数、GPU 表面帧数和 CPU 帧数。可使用 `--snapshot build/decoded.bmp` 保存最后一帧，仅用于本地诊断，正常播放不会逐帧回读 GPU。

目前播放单个固定尺寸、从关键帧开始的 P0 采集段；拒绝中途更换配置、倒退时间戳、异常长度和缺失关键帧。支持尺寸上限为 4096×4096，时间跨度最多一小时。

已在 iQOO Z9 Turbo / Android 16 的采集文件上验证：134 帧全部解码，GPU 与软件路径最后一帧逐像素一致。**尚未测量实时交互延迟、功耗或广泛设备兼容性。**

## 文档

- [当前技术设计](手机映射项目技术设计.md)
- [协议格式和原型范围](protocol/specification.md)
- [开发进度与下一步](docs/STATUS.md)
- [真机验证清单](docs/testing.md)
- [iQOO 采集与解码验证记录](docs/benchmarks/2026-10-22-iqoo-z9-turbo.md)

`scrcpy-project-summary.md` 和 v0.1 文档只保留为历史调研资料，不是当前实现依据。

## 隐私与发布

USB 预览仅通过已授权的 ADB 回环通道向本机电脑传输画面，不向互联网上传；本地采集文件保存在手机应用私有目录。导出的屏幕数据、密钥和本地构建产物已加入忽略规则。仓库公开不代表已选定开源许可证：目前未附加许可证，后续由作者决定授权方式。
