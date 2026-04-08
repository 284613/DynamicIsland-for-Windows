# Dynamic Island for Windows

> 将 iPhone 灵动岛体验移植到 Windows 桌面

## 项目简介

Dynamic Island for Windows 是一款基于 C++/Win32 API 和 Direct2D 构建的 Windows 桌面应用，旨在为 Windows 用户带来类似 iPhone 14 Pro 灵动岛的交互体验。

应用悬浮于屏幕顶部区域，以紧凑的胶囊形态呈现，能够智能检测并展示系统任务状态（音乐播放、通知提醒等），同时支持丰富的功能扩展。

### 适用场景

- **音乐爱好者**：实时显示 Spotify、网易云音乐等播放状态，无需切换窗口即可控制播放
- **效率办公**：文件传输监控、通知提醒，打造沉浸式工作环境
- **开发者/极客**：轻量级系统监控、快捷工具触手可及
- **多任务用户**：统一的交互入口，管理音乐、通知、天气等多种任务状态

---

## 功能列表

### 核心功能

| 功能模块 | 说明 |
|---------|------|
| **音乐播放器组件** | 显示专辑封面、歌曲名、歌手、播放进度条，支持播放/暂停/上一曲/下一曲控制 |
| **媒体会话监控** | 自动检测 Spotify、网易云音乐等主流音乐软件的播放状态（WinRT GlobalSystemMediaTransportControlsSessionManager） |
| **歌词监控** | 支持网易云音乐歌词获取与实时滚动显示（弹簧物理动画） |
| **提示弹窗组件** | WiFi 连接/断开、蓝牙设备切换、应用通知等场景提示，含 P0-P3 优先级调度 |
| **音量显示组件** | 悬停显示当前音量，支持拖动调节音量 |
| **系统监控** | 电量显示、充电状态、网络连接状态监控 |
| **通知监控** | 捕获并展示应用 Toast 通知消息 |
| **任务检测** | 统一检测各类任务状态，智能决定显示内容 |
| **天气插件** | 和风天气 API 获取实时天气，几何矢量图标，日夜自动切换 |
| **文件暂存面板** | 拖放文件自动接收，便捷的临时文件管理 |
| **插件管理** | 支持 IPlugin / IWeatherPlugin / IClockPlugin 插件扩展机制 |
| **全屏防打扰** | 检测全屏游戏/应用时自动隐藏（OPT-01） |
| **智能媒体轮询** | 播放中 1s / 暂停时 5s / 空闲时 10s 自适应轮询（OPT-02） |

### 窗口状态

| 状态 | 尺寸 | 说明 |
|-----|------|------|
| **Idle** | 80×28 px | 无任务时显示时间/天气 |
| **MusicCompact** | 200×40 px | 紧凑态，显示歌曲信息 |
| **MusicExpanded** | 340×160 px | 展开态，显示专辑封面、进度条、歌词 |
| **Alert** | 220×40 px | 通知提示弹窗 |
| **Volume** | 自适应 | 音量调节 OSD |
| **FileDrop** | 自适应 | 文件拖放指示 |

---

## 技术架构

### 技术栈

| 层级 | 技术 |
|-----|------|
| **语言** | C++17 |
| **GUI 框架** | Win32 API |
| **图形渲染** | Direct2D 1.1 + DirectComposition + D3D11 |
| **音频** | WASAPI（IAudioMeterInformation 实时音频电平） |
| **媒体控制** | Windows.Media.Control（WinRT） |
| **通知捕获** | Windows.UI.Notifications（WinRT） |
| **HTTP** | WinHTTP（支持 gzip 解压，依赖 zlib） |
| **构建系统** | MSBuild / Visual Studio 2022 v143 |
| **平台** | Windows 10/11 x64 |

### 架构分层

```
┌─────────────────────────────────────────────────┐
│              输入层（监控模块）                    │
│  MediaMonitor · LyricsMonitor · WeatherPlugin    │
│  ConnectionMonitor · NotificationMonitor         │
│  SystemMonitor · WindowManager                   │
└───────────────────────┬─────────────────────────┘
                        │ EventBus (Pub/Sub)
┌───────────────────────▼─────────────────────────┐
│              处理层（状态机）                      │
│  DynamicIsland（模式判定）                        │
│  LayoutController（弹簧物理 · 碰撞检测）           │
│  TaskDetector（任务聚合）                         │
└───────────────────────┬─────────────────────────┘
                        │ RenderContext（不可变数据）
┌───────────────────────▼─────────────────────────┐
│              渲染层（组件系统）                    │
│  RenderEngine → MusicPlayerComponent             │
│               → AlertComponent                   │
│               → VolumeComponent                  │
└─────────────────────────────────────────────────┘
```

### 核心模块

#### 主程序入口

| 文件 | 功能说明 |
|------|---------|
| `cpp/main.cpp` | 程序入口：初始化 OLE / WinRT 公寓 / DPI 感知，创建 DynamicIsland 实例 |
| `cpp/DynamicIsland.cpp` / `h/DynamicIsland.h` | 主窗口与消息循环：窗口注册、WndProc、状态管理、动画调度、告警队列、托盘图标、全屏检测（OPT-01） |
| `h/IslandManager.h` | 多岛管理器（Main / Mini / Pomodoro / Stats 四种岛类型） |
| `h/IslandState.h` | 岛屿状态枚举与 RenderContext 渲染上下文数据结构 |

#### 渲染引擎

| 文件 | 功能说明 |
|------|---------|
| `cpp/RenderEngine.cpp` / `h/RenderEngine.h` | D2D 渲染引擎：初始化 D3D11/D2D1.1/DirectComposition，`DrawCapsule()` 主绘制入口，TextLayout 缓存，DPI 缩放，天气图标绘制 |
| `h/Spring.h` | 弹簧物理引擎（张力/阻尼/质量可配置），工厂方法：`CreateBouncy / Smooth / Default` |
| `cpp/LayoutController.cpp` / `h/LayoutController.h` | 弹簧动画驱动的宽高过渡、按钮与进度条命中检测、布局矩形计算 |

#### 音乐播放

| 文件 | 功能说明 |
|------|---------|
| `cpp/MediaMonitor.cpp` / `h/MediaMonitor.h` | WinRT 媒体会话监控 + WASAPI 实时音频电平；智能轮询间隔（OPT-02）；播放控制 |
| `Components/MusicPlayerComponent.cpp/h` | 音乐播放器 UI：专辑封面、标题/艺术家、进度条（可拖动）、播放按钮、歌词区域；Compact / Expanded 双模式 |
| `cpp/LyricsMonitor.cpp` / `h/LyricsMonitor.h` | 网易云音乐歌词 API 搜索 + LRC 解析 + 时间轴同步；后台异步获取；弹簧物理滚动（OPT-05） |

#### 提示与状态

| 文件 | 功能说明 |
|------|---------|
| `Components/AlertComponent.cpp/h` | 提示弹窗渲染：WiFi、蓝牙、电量低、文件、App 通知；Segoe MDL2 图标 |
| `Components/VolumeComponent.cpp/h` | 音量显示组件：音量条、图标（静音/低/中/高）、拖动调节 |
| `cpp/ConnectionMonitor.cpp` / `h/ConnectionMonitor.h` | WiFi / 蓝牙连接状态变化检测 |
| `cpp/SystemMonitor.cpp` / `h/SystemMonitor.h` | 电量百分比、充电状态、WM_POWERBROADCAST 事件 |
| `cpp/NotificationMonitor.cpp` / `h/NotificationMonitor.h` | WinRT Toast 通知捕获 |

#### 任务与插件

| 文件 | 功能说明 |
|------|---------|
| `cpp/WeatherPlugin.cpp` / `h/WeatherPlugin.h` | 和风天气（QWeather）API 集成；gzip 解压；IP 位置自动获取；IWeatherPlugin 接口实现 |
| `cpp/TaskDetector.cpp` / `h/TaskDetector.h` | 统一任务检测：聚合各监控器状态，决定当前岛屿显示内容 |
| `h/PluginManager.h` | 插件管理器：IPlugin / IWeatherPlugin / IClockPlugin 接口，动态加载框架 |

#### 窗口面板

| 文件 | 功能说明 |
|------|---------|
| `cpp/SettingsWindow.cpp` / `h/SettingsWindow.h` | 设置窗口：通用/外观/主UI/文件面板/关于，配置持久化到 DynamicIslandSettings.ini |
| `cpp/FilePanelWindow.cpp` / `h/FilePanelWindow.h` | 文件暂存面板：拖放接收，暂存列表，支持删除 |

#### 基础设施

| 文件 | 功能说明 |
|------|---------|
| `h/Messages.h` | 自定义消息（WM_USER+100~206）、AlertInfo / ImageData 数据结构、AlertType / AlertPriority 枚举 |
| `h/Constants.h` | 统一常量命名空间：尺寸、颜色、动画参数 |
| `h/EventBus.h` | 事件总线（发布/订阅），线程安全，支持 12 种 EventType |
| `h/DropManager.h` | IDropTarget COM 实现，OLE 文件拖放 |
| `cpp/WindowManager.cpp` / `h/WindowManager.h` | Win32 窗口封装 |

### 颜色主题

| 用途 | RGB |
|-----|-----|
| 背景色 | (0.08, 0.08, 0.10) @ 80% 透明度 |
| WiFi 绿 | (0.1, 0.8, 0.3) |
| 蓝牙蓝 | (0.2, 0.5, 1.0) |
| 文件传输 | (0.1, 0.6, 1.0) |

---

## 天气图标系统

### 图标类型

| 天气类型 | 图标描述 |
|---------|---------|
| **晴天（Clear）** | 白天：几何太阳（圆形+8条射线）；夜晚：弯月+随机闪烁星星 |
| **多云（Cloudy）** | 几何云朵 |
| **雨天（Rainy）** | 云朵 + 底部雨滴 |
| **雪天（Snowy）** | 云朵 + 底部雪花 |

### 夜间模式

- **白天**：6:00 - 18:00 显示太阳图标
- **夜晚**：18:00 - 次日 6:00 显示弯月 + 随机分布的星星（带闪烁动画）

### 鼠标悬停触发动画

当岛屿处于展开状态且未播放音乐时，将鼠标移动到天气图标区域，会触发天气图标动画重播一次（`TriggerWeatherAnimOnce()`）。

---

## 通知优先级系统

告警按优先级排队处理，高优先级事件可打断当前显示：

| 优先级 | 说明 | 触发场景 |
|-------|------|---------|
| **P0_CRITICAL** | 可中断当前显示 | 低电量、网络断开 |
| **P1_IMMEDIATE** | 立即插队 | 微信、QQ、钉钉消息 |
| **P2_MEDIA** | 媒体控制 | 播放/暂停事件 |
| **P3_BACKGROUND** | 后台静默 | WiFi 已连接等 |

---

## 性能优化

| 优化项 | 状态 | 说明 |
|-------|------|------|
| **OPT-01** 全屏防打扰 | ✅ 完成 | 全屏游戏/应用时自动隐藏 |
| **OPT-02** 媒体轮询休眠 | ✅ 完成 | 智能轮询间隔（1s/5s/10s） |
| **OPT-03** 优先级告警调度 | ✅ 完成 | P0-P3 优先级队列 |
| **OPT-04** 动画驱动升级 | ⏳ 延期 | 独立渲染线程（架构改造较大） |
| **OPT-05** 歌词智能滚动 | ✅ 完成 | 弹簧物理滚动 + 末端减速 |

---

## 快速开始

### 环境要求

- Windows 10/11 x64
- Visual Studio 2022 Community（或更高版本），勾选「使用 C++ 的桌面开发」工作负载
- Windows SDK 10.0
- zlib（已包含在项目依赖中）

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

### 配置天气

在 `config.ini` 中填写和风天气 API Key 和城市信息：

```ini
[Weather]
APIKey=你的APIKey
LocationId=101250109
Longitude=112.93
Latitude=27.87
City=长沙
District=岳麓区
```

可从 [和风天气开发者平台](https://dev.qweather.com/) 免费申请 API Key。

---

## 项目结构

```
E:\vs\c++\DynamicIsland\
├── cpp/                           # C++ 实现文件（17个）
│   ├── main.cpp                   # 程序入口
│   ├── DynamicIsland.cpp          # 主窗口与消息循环
│   ├── RenderEngine.cpp           # Direct2D 渲染引擎
│   ├── LayoutController.cpp       # 弹簧布局 & 命中检测
│   ├── WindowManager.cpp          # Win32 窗口封装
│   ├── MediaMonitor.cpp           # WinRT 媒体监控 + WASAPI
│   ├── LyricsMonitor.cpp          # 网易云歌词获取与解析
│   ├── WeatherPlugin.cpp          # 和风天气 API 插件
│   ├── ConnectionMonitor.cpp      # WiFi/蓝牙监控
│   ├── NotificationMonitor.cpp    # Windows 通知监控
│   ├── SystemMonitor.cpp          # 电量/电源事件
│   ├── TaskDetector.cpp           # 任务聚合检测
│   ├── AlertComponent.cpp         # 提示弹窗渲染
│   ├── MusicPlayerComponent.cpp   # 音乐播放器 UI
│   ├── VolumeComponent.cpp        # 音量组件
│   ├── SettingsWindow.cpp         # 设置窗口
│   └── FilePanelWindow.cpp        # 文件暂存面板
│
├── h/                             # 头文件（25个）
│   ├── DynamicIsland.h            # 主类声明
│   ├── RenderEngine.h             # 渲染引擎接口
│   ├── LayoutController.h         # 布局控制器
│   ├── MediaMonitor.h             # 媒体监控接口
│   ├── PluginManager.h            # 插件系统接口
│   ├── WeatherPlugin.h            # 天气插件实现
│   ├── Constants.h                # 全局常量
│   ├── EventBus.h                 # 事件总线
│   ├── Messages.h                 # 自定义消息定义
│   ├── Spring.h                   # 弹簧物理引擎
│   ├── IslandState.h              # 状态枚举 & RenderContext
│   ├── LyricsMonitor.h            # 歌词监控接口
│   ├── NotificationMonitor.h      # 通知监控接口
│   ├── SystemMonitor.h            # 系统监控接口
│   ├── ConnectionMonitor.h        # 连接监控接口
│   ├── WindowManager.h            # 窗口管理器
│   ├── DropManager.h              # OLE 拖放实现
│   ├── TaskDetector.h             # 任务检测器
│   ├── IslandManager.h            # 多岛管理器
│   └── [其他组件头文件]
│
├── Components/                    # UI 渲染组件
│   ├── AlertComponent.cpp/h       # 提示弹窗
│   ├── MusicPlayerComponent.cpp/h # 音乐播放器
│   └── VolumeComponent.cpp/h      # 音量显示
│
├── vcxproj/                       # Visual Studio 项目文件
├── docs/                            # 设计文档
│   ├── OPT_STATUS.md              # 优化进度跟踪
│   ├── optimization-plan.md       # 优化方案设计
│   └── 阅读文档.md                # 源码阅读指南（中文）
│
├── DESIGN.md                      # 设计系统规范（Standardized Design Tokens & UI Specs）
├── DynamicIsland.sln              # VS 解决方案
├── DynamicIsland.vcxproj          # MSBuild 项目文件
├── Dynamic Island.rc              # Win32 资源文件
├── config.ini                     # 天气 API 配置
├── config.ini.example             # 配置模板
└── README.md                      # 本文档
```

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
| **透明度** | `WS_EX_LAYERED` + `SetLayeredWindowAttributes` 实现窗口透明度调节 |
| **主题色** | 背景透明色、主题色可配置 |
| **持久化** | 设置保存到 `DynamicIslandSettings.ini`，通过 `WM_USER+200` 消息通知主窗口实时应用 |

---

## 代码规模

| 模块 | 约行数 |
|------|--------|
| DynamicIsland.cpp | ~2000 |
| RenderEngine.cpp | ~1400 |
| MediaMonitor.cpp | ~1200 |
| SettingsWindow.cpp | ~750 |
| LyricsMonitor.cpp | ~600 |
| WeatherPlugin.cpp | ~350 |
| Components/ | ~1200 |
| 其他模块 | ~5800 |
| **合计** | **~13,300+** |

---

## 许可证

本项目仅供学习与研究使用。
