# Dynamic Island for Windows

> 将 iPhone Dynamic Island 体验移植到 Windows 桌面

## 项目简介

Dynamic Island for Windows 是一个基于 C++ / Win32 API / Direct2D 构建的桌面应用。它贴附在屏幕顶部，以 macOS 风格刘海主岛 + 独立胶囊副岛的方式承载音乐、通知、天气、文件暂存等交互。

当前代码已经完成组件化收尾：
- `RenderEngine` 只负责壳体、共享资源和组件调度
- 业务 UI 主要位于 `src/components/`
- `DynamicIsland.cpp` 已做一轮 slim，当前约 `1831` 行
- 文件副岛输入已拆到 `src/FileSecondaryInput.cpp`

## 核心能力

- 按需渲染：基于 `DirtyFlags`，空闲态停止定时器，减少 CPU 占用
- 主岛壳体：顶部平直贴顶，仅底部圆角；副岛保持独立胶囊
- mini / collapsed 形态：默认更扁的 `96x24`
- 音乐主岛：封面、标题、歌手、进度条、播放控制
- 多岛交互：音乐、音量、文件副岛可以协同出现
- 媒体监控：SMTC 事件优先，轮询兜底
- 歌词显示：滚动歌词与弹簧动画
- 天气面板：动态天气背景 + 小时 / 逐日预报
- 提示弹窗：WiFi、蓝牙、充电、低电量等优先级调度
- 文件副岛：mini / expanded / drop target，支持真实移动语义
- 设置窗口：Direct2D 全自绘，支持类别切换、滚动、保存并应用

## 技术栈

- 语言：C++17
- 图形渲染：Direct2D 1.1 + DirectComposition + D3D11
- 系统接口：Win32 API、WinRT、WASAPI
- 构建系统：MSBuild / Visual Studio 2022

## 架构概览

```text
输入层  MediaMonitor / NotificationMonitor / WeatherPlugin / SystemMonitor
调度层  DynamicIsland -> LayoutController
渲染层  RenderEngine + src/components/
```

职责边界：
- `DynamicIsland`：窗口消息、状态机、输入分发、配置回流
- `LayoutController`：尺寸、透明度、弹簧目标、命中测试
- `RenderEngine`：D2D 生命周期、壳体绘制、组件调度
- `src/components/`：天气、音乐、音量、歌词、文件、时钟等具体 UI

## 项目结构

```text
DynamicIsland/
├── src/
│   ├── components/             # 独立 UI 组件
│   ├── DynamicIsland.cpp       # 主窗口逻辑与状态调度
│   ├── FileSecondaryInput.cpp  # 文件副岛输入交互
│   ├── RenderEngine.cpp        # 渲染引擎壳体与调度
│   ├── LayoutController.cpp    # 弹簧布局与命中测试
│   ├── SettingsWindow.cpp      # D2D 自绘设置窗口
│   └── ...
├── include/
│   ├── components/             # 组件头文件
│   ├── settings/               # 设置控件模型
│   ├── FileStashStore.h        # 文件暂存存储层
│   ├── EventBus.h              # 事件总线
│   └── ...
├── REFACTOR_PLAN.md
├── SLIM_PLAN.md
├── DynamicIsland.sln
└── DynamicIsland.vcxproj
```

## 开发者速览

优先看这些文件：
- `src/DynamicIsland.cpp`：主窗口消息与模式切换
- `src/FileSecondaryInput.cpp`：文件副岛点击 / 悬停 / 拖拽 / 双击
- `src/LayoutController.cpp`：布局和命中测试
- `src/RenderEngine.cpp`：壳体和组件调度
- `src/SettingsWindow.cpp`：设置窗口

修改时的约束：
- 新功能优先放到对应组件，不要把业务状态重新塞回 `RenderEngine`
- 所有输入命中和布局坐标都要考虑 `m_dpiScale`
- DPI 坐标转换保持 `std::round` 语义，不要改成截断
- 天气请求必须异步，不能阻塞 UI 线程
- 文件暂存保持真实移动语义，目录在 `%LOCALAPPDATA%\\DynamicIsland\\FileStash\\`
- 设置窗口继续保持自绘模型，不要重新引入 Win32 子控件

## 配置

应用统一读取 exe 同目录的 `config.ini`。

常用配置节：
- `[Settings]`
- `[MainUI]`
- `[FilePanel]`
- `[Weather]`
- `[Notifications]`
- `[Advanced]`

`保存并应用` 会通过 `WM_SETTINGS_APPLY` 回流，当前会刷新：
- 通知白名单
- 天气配置
- 低电量阈值

## 当前状态

- 组件化重构已完成
- 主岛已切换为 macOS 刘海形态，顶部无间距
- 文件暂存已完全迁入副岛体系
- 设置窗口已切换到 Direct2D 全自绘
- 文件副岛输入已独立拆分
- `RenderEngine.cpp` 已瘦身为壳体 + 调度

## 编译与运行

环境要求：
- Windows 10/11 x64
- Visual Studio 2022 (v143)
- Windows SDK 10.0

构建命令：

```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
x64\Release\DynamicIsland.exe
```

## 许可证

本项目仅供学习与研究使用。
