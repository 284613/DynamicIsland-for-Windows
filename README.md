# Dynamic Island for Windows

> 将 iPhone 灵动岛体验移植到 Windows 桌面

## 项目简介

Dynamic Island for Windows 是一款基于 C++/Win32 API 和 Direct2D 构建的 Windows 桌面应用，旨在为 Windows 用户带来类似 iPhone 14 Pro 灵动岛的交互体验。

应用悬浮于屏幕顶部区域，以紧凑的胶囊形态呈现，能够智能检测并展示系统任务状态（音乐播放、番茄钟、通知等），同时支持丰富的功能扩展。

### 适用场景

- **音乐爱好者**：实时显示 Spotify、网易云音乐等播放状态，无需切换窗口即可控制播放
- **效率办公**：番茄钟计时、文件传输监控、通知提醒，打造沉浸式工作环境
- **开发者/极客**：轻量级系统监控、截图、计算器等快捷工具触手可及
- **多任务用户**：统一的交互入口，管理音乐、时钟、日历、提醒等多种任务状态

---

## 功能列表

### 核心功能

| 功能模块 | 说明 |
|---------|------|
| **音乐播放器组件** | 显示专辑封面、歌曲名、歌手、播放进度条，支持播放/暂停/上一曲/下一曲控制 |
| **媒体会话监控** | 自动检测 Spotify、网易云音乐等主流音乐软件的播放状态 |
| **歌词监控** | 支持网易云歌词获取与实时显示 |
| **提示弹窗组件** | WiFi 连接/断开、蓝牙设备切换、应用通知等场景提示 |
| **音量显示组件** | 悬停显示当前音量，支持拖动调节音量 |
| **番茄钟** | 内置 Pomodoro Timer，专注工作与休息切换 |
| **系统监控** | 电量显示、网络连接状态监控 |
| **通知监控** | 捕获并展示应用通知消息 |
| **任务检测** | 统一检测各类任务状态，智能决定显示内容 |
| **天气插件** | 天气数据获取与显示 |
| **文件暂存面板** | 拖放文件自动接收，便捷的临时文件管理 |
| **局域网传输** | 设备发现（UDP广播）+ 文件传输管理 |
| **插件管理** | 支持插件扩展机制 |

### 窗口状态

| 状态 | 尺寸 | 说明 |
|-----|------|------|
| **Mini** | 80×28 px | 无任务时显示时间 |
| **Collapsed** | 80×28 px | 收起状态 |
| **Expanded** | 340×120 px | 展开状态 |
| **Music Expanded** | 340×160 px | 音乐播放展开高度 |
| **Alert** | 220×40 px | 提示弹窗 |
| **Compact** | 200×40 px | 紧凑态 |

---

## 技术架构

### 技术栈

| 层级 | 技术 |
|-----|------|
| **语言** | C++ (C++17) |
| **GUI 框架** | Win32 API |
| **图形渲染** | Direct2D / DirectComposition |
| **构建系统** | MSBuild / Visual Studio 2022 |
| **平台** | Windows 10/11 x64 |

### 核心模块

#### 主程序入口

| 文件 | 功能说明 |
|------|---------|
| `main.cpp` | 程序入口，main 函数，初始化 HINSTANCE，创建窗口 |
| `DynamicIsland.cpp` / `.h` | 主窗口与消息循环：窗口类注册、WndProc（WM_CREATE/WM_PAINT/WM_MOUSEMOVE/WM_TIMER 等）、岛屿状态管理（Collapsed/Expanded/Alert）、所有子组件生命周期管理、DPI 处理、文件拖拽（WM_DROP_FILE）、托盘图标、热键注册（F10 打开设置）、音量条显示逻辑、悬停检测（HitTestCircles/HitTestPlaybackButtons/HitTestProgressBar） |
| `IslandManager.h` | 多岛管理器（Main/Mini/Pomodoro/Stats 四种岛类型），代码基本为内联实现 |
| `IslandState.h` | 岛屿状态数据结构定义 |

#### 渲染引擎

| 文件 | 功能说明 |
|------|---------|
| `RenderEngine.cpp` / `.h` | Direct2D 渲染引擎：初始化 Direct3D/Direct2D/DirectComposition 设备，DrawCapsule() 是主绘制入口，渲染岛屿背景（圆角矩形）、专辑封面、通知图标、播放控制按钮、进度条、任务圆圈指示器、歌词滚动；TextLayout 缓存；DPI 缩放处理 |
| `Spring.h` | 物理弹簧动画：基于张力/摩擦力的弹簧动画引擎（Tension: 0.15, Friction: 0.7），用于岛屿展开/收起动画 |

#### 音乐播放

| 文件 | 功能说明 |
|------|---------|
| `MediaMonitor.cpp` / `.h` | 媒体会话监控：通过 Windows Core Audio API（IMMDeviceEnumerator/IAudioSessionManager2）监听系统媒体会话，检测 Spotify/网易云等播放状态，获取歌曲名/艺术家/专辑封面，更新播放进度 |
| `MusicPlayerComponent.cpp` / `.h` | 音乐播放器 UI 组件：专辑封面 + 歌曲名 + 艺术家 + 进度条 + 播放按钮 + 歌词；支持 Expanded 和 Compact 两种模式；文字过长时自动滚动 |
| `LyricsMonitor.cpp` / `.h` | 歌词监控：通过 HTTP 请求抓取网易云音乐歌词，解析 [00:xx.xx] 时间轴，实时更新当前播放行 |
| `SpotifyClient.h` | Spotify Web API 客户端（备用方案） |

#### 提示与状态

| 文件 | 功能说明 |
|------|---------|
| `AlertComponent.cpp` / `.h` | 提示弹窗组件：WiFi 连接/断开、蓝牙配对、电量低、文件传输进度、App 通知等场景提示；AlertInfo 结构体包含 type、name、deviceType |
| `VolumeComponent.cpp` / `.h` | 音量显示组件：悬停显示当前音量百分比，支持拖动调节；音量图标根据音量级动态切换 |
| `ConnectionMonitor.cpp` / `.h` | 网络连接监控：检测 WiFi / 以太网连接状态变化，触发 AlertInfo 推送 |
| `SystemMonitor.cpp` / `.h` | 系统状态监控：电量百分比、充电状态、CPU/内存占用率 |
| `NotificationMonitor.cpp` / `.h` | Windows 通知监控：捕获第三方应用通知，触发 AlertComponent 显示 |
| `VolumeOSDController.h` | Windows 系统音量 OSD 控制 |

#### 任务与功能

| 文件 | 功能说明 |
|------|---------|
| `WeatherPlugin.cpp` / `.h` | 天气插件：获取并显示天气数据 |
| `PomodoroTimer.h` | 番茄钟：工作/休息时间倒计时，WM_USER 消息触发开始/暂停/切换状态 |
| `TaskDetector.cpp` / `.h` | 任务检测器：统一检测各类活跃任务状态（音乐播放/番茄钟/文件传输等），决定当前岛屿显示内容 |

#### 窗口面板

| 文件 | 功能说明 |
|------|---------|
| `SettingsWindow.cpp` / `.h` | 设置窗口：独立对话框，创建导航按钮（通用/外观/主UI/文件面板/关于）和各类控件（滑块、复选框）；设置通过 DynamicIslandSettings.ini 持久化；ApplySettings() 通过 WM_USER+200 消息通知主窗口 |
| `FilePanelWindow.cpp` / `.h` | 文件暂存面板：拖拽文件到岛屿上时显示的接收面板，暂存文件路径列表，支持删除已暂存文件 |

#### 传输与插件

| 文件 | 功能说明 |
|------|---------|
| `NetworkDiscovery.h` | 局域网设备发现：UDP 广播发现局域网内其他设备 |
| `LanTransferManager.h` | 局域网文件传输：管理与其他发现设备之间的文件上传/下载 |
| `PluginManager.h` | 插件管理器：支持动态加载第三方插件（框架实现） |

#### 基础设施

| 文件 | 功能说明 |
|------|---------|
| `Messages.h` | 窗口消息定义：WM_USER+100~WM_USER+206 自定义消息常量；AlertInfo 和 ImageData 数据结构；AlertType 枚举 |
| `Constants.h` | 全局常量：岛屿尺寸、颜色主题 RGB 值、动画参数、Spring 物理参数、网络端口号等 |
| `EventBus.h` | 事件总线：组件间通信的发布/订阅机制 |
| `ThreadSafeQueue.h` | 线程安全队列：跨线程安全数据传递，用于后台线程向主窗口传递数据 |
| `WindowManager.cpp` / `.h` | 窗口管理器：封装 CreateWindowEx 等 Win32 API |
| `IMessageHandler.h` | 消息处理器接口：定义组件处理窗口消息的接口协议 |
| `DropManager.h` | 文件拖放管理器：处理 WM_DROP_FILE 拖放消息，管理暂存文件列表 |
| `Dynamic Island.rc` | Win32 资源文件：应用图标、版本信息等 |
| `Resource.h` | 资源头文件：资源 ID 常量定义 |

#### UI 组件（Components/）

| 文件 | 功能说明 |
|------|---------|
| `AlertComponent.cpp` / `.h` | 提示弹窗组件（已在主表中描述） |
| `MusicPlayerComponent.cpp` / `.h` | 音乐播放器 UI 组件（已在主表中描述） |
| `VolumeComponent.cpp` / `.h` | 音量显示组件（已在主表中描述） |

### 颜色主题

| 用途 | RGB |
|-----|-----|
| 背景色 | (0.08, 0.08, 0.10) @ 80% 透明度 |
| 主题色 | (1.0, 0.2, 0.4) 红色系 |
| WiFi | (0.1, 0.8, 0.3) 绿色 |
| 蓝牙 | (0.2, 0.5, 1.0) 蓝色 |
| 文件传输 | (0.1, 0.6, 1.0) 天蓝色 |

---

## 快速开始

### 环境要求

- Windows 10/11 x64
- Visual Studio 2022 Community（或更高版本）
- 安装时需勾选「使用 C++ 的桌面开发」工作负载

### 编译

```batch
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
```

或通过 Visual Studio 2022 打开 `DynamicIsland.sln`，生成 → 重新生成解决方案，平台选择 **x64**。

### 运行

编译完成后可执行文件位于：

```
x64\Release\DynamicIsland.exe
```

双击运行即可启动应用。

---

## 项目结构

```
E:\vs\c++\DynamicIsland\
├── Components/                # UI 组件
│   ├── AlertComponent.cpp/h    # 提示弹窗
│   ├── MusicPlayerComponent.cpp/h  # 音乐播放器
│   └── VolumeComponent.cpp/h   # 音量显示
├── docs/                       # 设计文档
│   └── superpowers/            # 项目规划与设计
├── icon/                       # 图标资源
├── x64/                        # 编译输出目录
├── DynamicIsland/               # x64 Release 编译产物
├── DynamicIsland.sln           # VS 解决方案
├── DynamicIsland.vcxproj       # VS 项目文件
├── Dynamic Island.rc           # Win32 资源文件
├── main.cpp                    # 程序入口
├── DynamicIsland.cpp/h         # 主窗口与消息循环
├── RenderEngine.cpp/h          # Direct2D 渲染引擎
├── MediaMonitor.cpp/h          # 媒体会话监控
├── MusicPlayerComponent.cpp/h  # 音乐播放器 UI
├── LyricsMonitor.cpp/h         # 歌词监控
├── AlertComponent.cpp/h        # 提示弹窗
├── VolumeComponent.cpp/h       # 音量显示
├── SettingsWindow.cpp/h        # 设置窗口
├── FilePanelWindow.cpp/h       # 文件暂存面板
├── ConnectionMonitor.cpp/h     # 网络连接监控
├── SystemMonitor.cpp/h         # 系统状态监控
├── NotificationMonitor.cpp/h   # Windows 通知监控
├── WeatherPlugin.cpp/h        # 天气插件
├── TaskDetector.cpp/h          # 任务检测器
├── PomodoroTimer.h             # 番茄钟
├── NetworkDiscovery.h           # 局域网设备发现
├── LanTransferManager.h        # 局域网文件传输
├── PluginManager.h             # 插件管理器
├── IslandManager.h             # 多岛管理器
├── IslandState.h               # 岛屿状态定义
├── Messages.h                  # 窗口消息定义
├── Constants.h                 # 全局常量
├── EventBus.h                  # 事件总线
├── ThreadSafeQueue.h           # 线程安全队列
├── Spring.h                    # 弹簧动画引擎
├── WindowManager.cpp/h          # 窗口管理器
├── IMessageHandler.h           # 消息处理器接口
├── DropManager.h               # 文件拖放管理
├── SpotifyClient.h             # Spotify API 客户端
├── VolumeOSDController.h       # 系统音量 OSD 控制
├── README.md                   # 本文档
└── [其他 .h]                   # 头文件（见上表）
```

---

## 天气图标系统

### 图标类型

| 天气类型 | 图标描述 |
|---------|---------|
| **晴天（Clear）** | 白天：几何太阳（圆形+8条射线）；夜晚：根据系统时间自动切换为弯月+星星 |
| **多云（Cloudy）** | 几何云朵 |
| **雨天（Rainy）** | 云朵+底部雨滴 |
| **雪天（Snowy）** | 云朵+底部雪花 |
| **小雪（Light Snow）** | 较小雪花图标 |

### 夜间模式

天气图标根据系统时间自动判断白天/夜晚：
- **白天**：6:00 - 18:00 显示太阳图标
- **夜晚**：18:00 - 次日 6:00 显示弯月 + 随机分布的星星（带闪烁动画）

### 鼠标悬停触发动画

当岛屿处于展开状态且未播放音乐时，将鼠标移动到天气图标区域，会触发天气图标动画重播一次。动画包括太阳旋转、星星闪烁等效果。

### 文件说明

| 文件 | 功能说明 |
|-----|---------|
| `WeatherPlugin.cpp` / `.h` | 天气插件：获取天气数据并设置天气描述文字 |
| `RenderEngine.cpp` / `.h` | 天气图标渲染：`DrawWeatherIcon()` 方法，绘制几何天气图标；`MapWeatherDescToType()` 将中文天气描述映射为 `WeatherType` 枚举；支持白天/夜晚自动切换；`TriggerWeatherAnimOnce()` 方法响应鼠标悬停触发动画 |

---

## 键盘快捷键

| 快捷键 | 功能 |
|-------|------|
| **F10** | 打开设置窗口 |
| **Ctrl+Alt+Shift+S** | 打开设置窗口（备用） |

---

## 设置窗口

| 功能 | 说明 |
|-----|------|
| **宽度/高度** | 通过 `SetWindowPos` 实时调整岛屿窗口尺寸 |
| **透明度** | 通过 `WS_EX_LAYERED` + `SetLayeredWindowAttributes` 实现窗口透明度调节 |
| **主题色** | 背景透明色、主题色可配置 |
| **通用设置** | 通用配置选项卡 |
| **主UI设置** | 主界面尺寸、高度等配置 |

设置窗口通过 `DynamicIslandSettings.ini` 持久化，`ApplySettings()` 通过 `WM_USER+200` 消息通知主窗口实时应用更改。

---

## 配置文件

| 文件 | 说明 |
|-----|------|
| `DynamicIslandSettings.ini` | 主 UI 设置：主题色、透明度、宽度、高度 |
| `config.ini` | 通用配置文件 |

---

## 许可证

本项目仅供学习与研究使用。

