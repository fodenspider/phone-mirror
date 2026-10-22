# 运行依赖与分发说明

- Windows 桌面端：项目自身 C++ 代码；使用系统 Win32、Media Foundation、D3D11 和 Winsock。官方 Windows 构建使用 MSVC 静态运行库，无需安装浏览器运行时。
- Android 伴侣：项目自身 Kotlin 代码、Android 系统 API 和 Kotlin 标准库。Kotlin 使用 Apache-2.0 许可，项目主页 https://github.com/JetBrains/kotlin 。Android APK 构建所附标准元数据保留在包中。
- Google Platform-Tools：不打入本项目安装包。用户从 Android 官方页面获取原始 ZIP；导入程序固定校验版本摘要，保留原包 NOTICE.txt 和 source.properties。应遵守官方 SDK 下载条款；本项目不替用户接受条款。
- Inno Setup：仅用于生成 Windows 安装包。项目主页 https://jrsoftware.org/isinfo.php 。安装/卸载引擎随其生成的包提供，遵循其分发条款；中文主要流程提示为本项目编写。
- 测试与构建：CMake、编译器、Gradle、Android Gradle Plugin、JUnit 仅用于开发/构建，未要求终端用户安装。

本清单不替代各依赖自己的许可文本。正式公开发行前应完成许可证和依赖清单复核。当前仓库尚未由维护者指定项目自身的开源许可证，公开可见不代表已授予任意再分发许可。
