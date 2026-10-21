# Phone Mirror

独立实现的 Android → Windows 手机映射项目，目标是低延迟、低占用的日常投屏和控制。

**当前为 P0 开发阶段，不是可直接使用的投屏软件。** 不包含或复用 scrcpy 的代码、服务端、可执行程序或内部协议。

## 当前已经实现

- C++20 协议库：40 字节大端头、类型长度上限、半包/粘包处理、异常流拒绝。
- C++ / Kotlin 共用协议测试向量，边界、截断、错误头和随机变异测试。
- Windows 原生窗口工程，以及离线协议文件检查工具 `pm-inspect`。
- Android 采集原型：用户授权后，通过 Surface 输入硬件 H.264 编码，最多采集 15 秒，写入应用私有目录的 `capture.pmstream`。
- GitHub Actions：Windows / Linux C++ 构建测试，以及 Android 单测、Lint、调试 APK 构建。工作流是否成功以仓库 Actions 实际结果为准。

**尚未实现**：USB 实时传输、Windows 视频解码显示、远程手势/中文输入、音频、无线配对和完整录像。窗口不会显示手机画面，安卓原型不会启动网络服务。

## 技术路线

```text
Android: Kotlin + MediaProjection + MediaCodec
                   ↓ 自研 PMIR 协议（USB 通道待接入）
Windows: C++20 + Media Foundation + D3D11 / DXGI（媒体后端待接入）
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

`run-as` 仅用于调试构建。脚本以原始字节写文件，避免 PowerShell 文本重定向破坏视频数据。检查器当前验证消息边界和长度，**不验证 H.264 是否可解码**；真机画质和延迟还没有测量。

## 文档

- [当前技术设计](手机映射项目技术设计.md)
- [协议格式和原型范围](protocol/specification.md)
- [开发进度与下一步](docs/STATUS.md)
- [真机验证清单](docs/testing.md)

`scrcpy-project-summary.md` 和 v0.1 文档只保留为历史调研资料，不是当前实现依据。

## 隐私与发布

原型不联网、不上传画面；采集文件保存在手机应用私有目录。导出的屏幕数据、密钥和本地构建产物已加入忽略规则。仓库公开不代表已选定开源许可证：目前未附加许可证，后续由作者决定授权方式。
