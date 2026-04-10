# Dynamic Island for Windows

> 将 iPhone 灵动岛体验移植到 Windows 桌面

## 项目简介

Dynamic Island for Windows 是一款基于 C++/Win32 API 和 Direct2D 构建的 Windows 桌面应用，旨在为 Windows 用户带来类似 iPhone 14 Pro 灵动岛的交互体验。

应用悬浮于屏幕顶部区域，以紧凑的胶囊形态呈现，能够智能检测并展示系统任务状态（音乐播放、通知提醒等），同时支持丰富的功能扩展。当前版本已完成组件化收尾，渲染引擎只负责壳体、共享资源、组件调度和副岛绘制。

---

## 功能列表

### 核心功能

| 功能模块 | 说明 |
|---------|------|
| **按需渲染引擎** | [HOT] 核心重构：引入 DirtyFlags 标脏机制。空闲态定时器完全停止，实现 0% CPU 占用，仅在有变化或动画时渲染。 |
| **音乐播放器组件** | 显示专辑封面、歌曲名、歌手、播放进度条，支持播放/暂停/上一曲/下一曲控制 |
| **多岛交互模式** | [NEW] 支持多岛协同显示（如音乐展开时调节音量，下方弹出独立副岛），含平滑弹簧动画与 Region 同步更新 |
| **媒体封面缓存** | [NEW] 专辑封面支持本地磁盘缓存，并在首次展开音乐面板时主动补同步当前封面 |
| **媒体会话监控** | 自动检测 Spotify、网易云音乐等主流音乐软件的播放状态（WinRT SMTC 会话），已改为**事件唤醒优先、轮询兜底**，显著缩短“开始播放后主岛切到音乐态”的响应时间 |
| **歌词监控** | 支持网易云音乐歌词获取与实时滚动显示（含本地缓存与弹簧动画） |
| **音量调节优化** | 仅在播放音乐时允许调节，展开态支持副岛反馈，避免全局误触 |
| **提示弹窗组件** | WiFi、蓝牙、充电、低电量等场景提示，含 P0-P3 优先级调度 |
| **系统监控** | 实时电量、网络连接、全屏应用检测自动隐藏 |
| **天气插件** | 和风天气数据源，API Key 从 `config.ini` 读取。展开面板左卡已重构为**场景式动态天气背景**：以天空、地平线、山体、森林边线和天气介质层为主，按清晨/白天/傍晚/夜晚切换色调；雨雪会根据天气强度动态调整粒子数量、尺寸和速度。右卡显示逐小时预报（时间 + 矢量图标 + 温度），鼠标滚轮可在**逐小时 ↔ 逐日（7天）**预报视图之间切换。 |
| **文件中转站** | 已重构为**文件副岛**：文件存在时常驻 mini 副岛，不影响主岛当前模式；点击可展开，支持系统图标/缩略图、单击预览、双击打开，以及真实移动语义的拖入/拖出暂存 |

---

### 项目结构

```
DynamicIsland/
├── src/
│   ├── components/             # 独立 UI 组件（IIslandComponent 接口）
│   │   ├── WeatherComponent    # 天气展开面板 + 场景式动态天气背景
│   │   ├── MusicPlayerComponent# 专辑封面 + 歌词 + 进度条 + 按钮
│   │   ├── AlertComponent      # 通知/WiFi/蓝牙/充电等提示卡片
│   │   ├── VolumeComponent     # 主岛音量条 + 副岛音量
│   │   ├── LyricsComponent     # 歌词滚动 + 弹簧物理
│   │   ├── WaveformComponent   # 三柱音频波形动画
│   │   ├── FilePanelComponent  # 文件副岛（mini / expanded / drop target）
│   │   └── ClockComponent      # 紧凑态时钟
│   ├── RenderEngine.cpp        # D2D 渲染引擎（~354 行，壳体 + 调度）
│   ├── DynamicIsland.cpp       # 主窗口逻辑与状态机
│   ├── LayoutController.cpp    # 弹簧布局控制器
│   └── ...
│
├── include/
│   ├── components/
│   │   ├── IIslandComponent.h  # 统一组件接口 + SharedResources
│   │   └── *.h                  # 各组件头文件
│   ├── FileStashStore.h        # 文件暂存存储层（真实移动、最多 5 个文件）
│   ├── IslandState.h           # 瘦身后的 RenderContext + 状态枚举
│   ├── EventBus.h              # 线程安全事件总线
│   └── ...
│
├── REFACTOR_PLAN.md            # 组件化重构计划（已完成）
├── DynamicIsland.sln
└── DynamicIsland.vcxproj
```

### 技术架构

- **语言**: C++17
- **图形渲染**: Direct2D 1.1 + DirectComposition + D3D11
- **系统接口**: Win32 API, WinRT (Media/Notifications), WASAPI
- **构建系统**: MSBuild / Visual Studio 2022

### 组件化重构进度

**✅ 全部完成（PR1-PR6 + 收尾修正）**

| PR | 内容 | 状态 |
|----|------|------|
| PR1 | `IIslandComponent` 接口 + `SharedResources` + `RegisterComponents()` | ✅ 完成 |
| PR2 | `WeatherComponent`（天气全部绘制 + 动画） | ✅ 完成 |
| PR3 | `LyricsComponent` / `WaveformComponent` | ✅ 完成 |
| PR4 | `MusicPlayerComponent` / `AlertComponent` / `VolumeComponent` 实现新接口 | ✅ 完成 |
| PR5 | `FileStorageComponent` / `ClockComponent` | ✅ 完成 |
| PR6 | 优先级调度表 + `RenderContext` 瘦身 + EventBus 数据流 | ✅ 完成 |

详细计划见 [REFACTOR_PLAN.md](REFACTOR_PLAN.md)。

**重构效果：**
- `RenderEngine.cpp`: `5684` 行 → `354` 行
- `include/RenderEngine.h`: `194` 行 → `105` 行
- `include/IslandState.h`: `100` 行 → `40` 行
- `RenderContext` 只保留布局/透明度/模式/时间戳

**当前实现特征：**
- `DynamicIsland` 每帧只把业务状态同步给组件，再传递瘦身后的 `RenderContext`
- `RenderEngine` 不再保留天气、歌词滚动、专辑图、通知图、文件 UI 等业务私有状态
- 天气展开态滚轮只影响天气视图，不再透传到底层音乐控件
- 音量副岛由引擎画壳体、`VolumeComponent` 只画内容
- 文件暂存已迁入副岛体系：mini 常驻、expanded 展开、drop target 拖入提示，不再劫持主岛模式
- `MediaMonitor` 已接入 SMTC 事件监听（会话、播放状态、媒体属性、时间线），轮询只负责兜底同步

---

## 快速开始

### 环境要求

- Windows 10/11 x64
- Visual Studio 2022 (v143)
- Windows SDK 10.0

### 编译与运行

1. 使用 Visual Studio 打开 `DynamicIsland.sln`。
2. 选择 **Release** 配置与 **x64** 平台。
3. 点击 **生成解决方案**。
4. 运行生成的 `x64\Release\DynamicIsland.exe`。

---

## 许可证

本项目仅供学习与研究使用。
