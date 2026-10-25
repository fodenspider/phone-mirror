# 2026-10-25：0.6 连续触摸预览开发

已实现新版手机能力识别、鼠标按下/持续位置/松开消息、Android 分段连续触摸与 500 ms 心跳过期释放，保留滚轮四档设置及旧手机应用回退。桌面本地 11 项检查通过（6.66 秒）；`f4fd1ee` 的 Windows、Linux、Android 和打包 CI 全部成功，0.6（versionCode 5）已替换安装到测试手机。长按、跟手拖动和撤权释放真机验收仍待用户反馈。见[实现与边界](continuous-touch.md)。

# 2026-10-25：滚轮幅度设置

新增“更多 → 滚轮幅度”四档（6% / 12% / 20% / 30%），立即生效并保存，默认 12%。本地 11 项检查通过。用户确认上一版滚轮合适；所谓点击迟缓具体为按住时无动作、松开才触发，与现有完整手势发送方式一致。长按和连续跟手拖动仍未实现。

# 2026-10-25：鼠标响应优化待验收

用户指出画面同步正常，但鼠标操作迟缓、滚轮过于灵敏。已缩小滚轮幅度，并使控制消息立即非阻塞发送，避免依赖视频读取进度。本地 11 项检查通过；实际手感待用户比较，拖动仍采用松开后执行。详见[验证记录](device-validation-20261025.md)。

# 2026-10-25：60 秒前台与后台连接续测

均衡档前台静止页和后台公开测试页各完成一次 60 秒接收，分别解码 134 和 873 帧，均为 576×1280、全部 GPU 输出，正常退出。未重现此前认证超时，但尚未证明偶发问题已解决。用户另已确认新版桌面入口连接、独立点击、手机系统停止共享及同窗口重新授权恢复正常，USB 拔线后提示中断且插回重连成功。用户还确认未勾选本次控制权限时，投屏正常且点击、滚轮均不能控制手机。`2b943ec` 的 Windows、Linux、Android 与安装包 CI 全部通过。详见[本轮验证记录](device-validation-20261025.md)。

# 2026-10-24：0.5.0-preview 真机验证

`298411fa` 的 Android、Windows、Linux 与打包 CI 全部成功。`0.5.0-preview`（versionCode 4）已安装到 iQOO Z9 Turbo / Android 16；用户手动确认实时画面、鼠标滚轮、拖动松开控制正常。省资源档与清晰档均通过约 20 秒前台静止页接收解码测试，分别为 432×960 的 43 帧和 864×1920 的 46 帧，全部以 GPU 表面输出；此前认证超时原因仍待定位。清晰档结束后未发现 CaptureService 或有效会话 JSON。独立点击、动态画质、稳定重连及断线/撤权/长时测试尚未验收。接收进程短时资源采样及验证限制见[真机验证记录](device-validation-20261024.md)。桌面端缺失会话文件读取退出码为 0 的授权等待误判另行修复并单独验证，不作为认证超时根因。以下离线阶段说明与历史记录保留供追溯。

# 0.5 离线产品完善

本地 11 项 CTest 已通过，覆盖设置恢复、模拟设备/画质传参、离线演示隔离、三种缩放原生绘制、协议和传输。Android 与安装包更新仍需当前提交的远程构建确认。

已补齐三档画质配置到 Android 编码参数的传递、离线演示、设置恢复、版本下载入口、依赖说明及发布门槛。内部绘制的三种缩放界面已目视检查；不替代实际显示器切换。Android 新增画质单元测试，打包新增重装和卸载保留设置检查，远程验收以当前提交 Actions 结果为准。全程未连接手机。

以下为此前阶段记录：

# Development status

Updated: 2026-10-22. Milestone: live-video device baseline verified; novice-oriented desktop connection and gesture preview under validation.

## Implemented

- Independent C++ and Kotlin message header codecs with type-specific allocation bounds.
- Incremental C++ parser for fragmented and coalesced input; fatal handling of malformed streams.
- Shared wire fixture, truncation/bounds/version tests and deterministic header mutations.
- Native Windows local-capture player with timestamp pacing, aspect-preserving scaling, stop/reopen and software-decode option.
- Windows Media Foundation H.264 decoder connected to D3D11 video processing / DXGI presentation.
- GPU surface output avoids per-frame CPU readback; software output has an explicit upload path.
- Strict capture configuration, sequence, timestamp and initial-IDR validation.
- Offline framing inspector plus an actual decode/render verification tool.
- Android user-consented, foreground-service Surface/H.264 capture probe with 15-second and 32-MiB limits.
- Android loopback-only USB stream with one-time 128-bit authentication, bounded queue and explicit user consent. No LAN listener or background startup. Gesture control now has a separate accessibility permission and per-session opt-in.
- Windows cancellable USB receiver and launcher; credential passes through an inherited pipe, forwarding is cleaned up at receiver exit.
- Build/test workflow and development documentation.

## Not implemented / not claimed

- Real-device live video: 151 received/decoded GPU frames in 20,060 ms at 576×1280. The user subsequently confirmed visible live updates. This is not a latency measurement.
- Desktop direct connection and gesture controls are implemented but still require later device validation.
- No verified gesture behavior on a real device yet; no IME, audio or automatic keyframe recovery.
- No measured device compatibility, latency or power figures.
- A single device capture has been decoded successfully; permission revocation, broader encoder behavior and other devices still need testing.

## Verified build baseline

- Desktop guidance/gesture preview at `0752a51`: Windows/MSVC, Linux and Android unit tests/lint/APK all passed in [CI run 39776907129](https://github.com/fodenspider/phone-mirror/actions/runs/39776907129).
- Five local test groups passed, including fake-device connection/error/cancel scenarios. Offline GPU/software playback of 349 frames and hidden-window resize/reopen/cancel regression passed. No phone was accessed for this round.
- Updated UI visual acceptance remains pending: background-only screenshot capture returned no usable rendering and is not counted as a visual pass.

- USB preview source at `29e72d7`: Android JVM tests/lint/APK, Windows/MSVC tests and Linux tests all passed in [CI run 39762569499](https://github.com/fodenspider/phone-mirror/actions/runs/39762569499).
- Local authenticated loopback replay sent the existing capture at its original timestamps: 349 input / 349 decoded frames, all using GPU surfaces, clean STOP. This validates the receiver integration, not live phone capture or end-to-end latency.
- After the stream-source refactor, the same 349-frame capture still passed GPU/software decoding, identical final-frame comparison and hidden-window resize/reopen/cancel smoke tests.
- Local Windows MinGW-w64 Release build and CTest passed.
- Commit `005003e`: GitHub Windows/MSVC and Linux builds/tests passed; Android unit tests, lint and debug APK build passed.
- [Verified CI run and downloadable development artifacts](https://github.com/fodenspider/phone-mirror/actions/runs/39596351558).
- Android 16 / API 36 on iQOO Z9 Turbo: exported 134 frames at 576×1280 and validated sequence/timestamps.
- Local GPU-surface and software decoder paths both decoded/rendered all 134 frames. Their final rendered frames match exactly.
- Hidden-window playback smoke test completed playback, resized, reopened, cancelled and exited successfully.
- A fresh USB-exported capture was retested: all 349 frames decoded on both paths, final BMP files matched, and hidden-window playback/resize/reopen/cancel passed. [Retest details](benchmarks/2026-10-22-usb-retest.md).
- No latency or power consumption result is claimed. See the dated benchmark record for limits.

## Next vertical slice

Follow [the product plan](product-plan.md): a novice-friendly, calm native UI with no command-line requirement for daily use.

1. Finish desktop connection guidance, installation/update flow, multi-device choice and packaging.
2. Run automated and simulated-device checks without a connected phone.
3. Resume real-device control, reconnection, permission and performance tests only when the user says a phone is available.

Repository source is original to this project; earlier scrcpy documents are research history only.
# 2026-10-22：0.4 产品入口与打包进展

后续增量：多设备手动刷新与选择已接入窗口，安装和连接使用相同的显式目标；目标断开后拒绝自动改连。使用独立模拟设备进程验证，不接触真实手机。远程构建仍需以 Actions 实际结果为准。

0.4.1：新增设置保存/损坏恢复、本机组件与安装包准备检查、可滚动可键盘阅读的说明区、工作区内启动尺寸、安装主要流程中文化。8 项本地测试通过；349 帧离线播放、缩放、重开和取消回归通过。隐藏空闲窗口 10.01 秒采样工作集 13.37 MiB，私有内存 2.46 MiB，CPU 增量在测量精度内为 0；不能替代投屏性能数据。隐藏截图黑图，完整视觉验收仍未完成。未连接手机。

新增四步引导、组件导入、手机应用安装入口、状态页面和安全诊断复制；原生轻量窗口不引入浏览器运行时。

本地 7 项自动测试通过，包含无手机的窗口流程与模拟传输。官方 Platform-Tools 37.0.1 导入及非法摘要拒绝通过。安装包流水线新增隔离安装/启动/卸载验证，远程结果以对应 Actions 运行记录为准。

尚未完成：完整缩放视觉验收、设备选择与画质设置、正式签名，以及真机控制和性能验收。此轮不连接手机。

