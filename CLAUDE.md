# CLAUDE.md - Dynamic Island for Windows

Project guide and instructions for AI assistants.

## 项目背景 (Project Background)

**Dynamic Island for Windows** 是一款旨在将 iPhone 14 Pro 的“灵动岛”交互体验移植到 Windows 10/11 系统的桌面应用。

- **核心价值**：在屏幕顶部提供一个美观、非侵入式的交互入口，集中展示系统状态（音乐、通知、音量、天气、文件传输等）。
- **目标用户**：追求桌面美化、高效多任务处理以及希望获得沉浸式 Windows 使用体验的用户。

## 技术架构 (Technical Architecture)

项目采用解耦的分层架构设计，确保监控模块与 UI 渲染的高度分离。

### 1. 架构分层
- **输入层 (Monitoring)**：负责捕获外部事件。包括 `MediaMonitor`, `NotificationMonitor`, `WeatherPlugin`, `SystemMonitor` 等。
- **处理层 (State Machine)**：负责逻辑判定与布局计算。
  - `TaskDetector`：聚合各监控模块状态。
  - `LayoutController`：基于弹簧物理 (`Spring.h`) 的平滑布局过渡。
  - `DynamicIsland`：主控类，管理窗口生命周期及优先级调度。
- **渲染层 (Component System)**：基于组件化的渲染设计。
  - `RenderEngine`：Direct2D 核心绘制引擎。
  - `src/components/`：各个功能模块的绘制实现。

### 2. 核心机制
- **事件总线 (EventBus)**：各模块间通过线程安全的发布/订阅模式通信。
- **弹簧物理 (Spring Physics)**：UI 动画由弹簧算法驱动，提供流体感。
- **按需渲染 (On-Demand Rendering)**：引擎不再采用固定 16ms 轮询。通过 `DirtyFlags` 标脏机制与事件驱动（如媒体状态变更事件）按需唤醒渲染。在岛屿稳定且无交互的空闲态，定时器完全停止，实现 0% CPU 占用。
- **天气详情增强 (Weather Detail)**：支持天气图标 Hover 交互触发轻量动画，点击可展开为 400x180 的左右分栏玻璃拟态面板，右侧 2×3 网格显示 6 小时逐小时预报（时间 / 温度 / 天气描述），左侧显示实时天气与生活建议。
- **多岛交互 (Multi-Island)**：支持主副双岛协同显示（如音乐展开态下的音量弹出），通过 Win32 Region 合并实现。
- **本地缓存 (Caching)**：专辑封面与歌词支持本地持久化缓存，提升二次加载速度。
- **图形栈**：`Direct2D 1.1` + `DirectComposition` + `D3D11`。

## Build Commands

- **Build with MSBuild:** `msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64`
- **Run Executable:** `x64\Release\DynamicIsland.exe`

## Project Structure

- `src/`: Core logic and window implementation (`.cpp`).
- `src/components/`: UI rendering components implementation.
- `include/`: Header files and interfaces (`.h`).
- `include/components/`: UI rendering components headers.
- `resources/`: Icon assets and `.rc` scripts.
- `docs/`: Design and optimization documentation.

## 开发规范 (Development Guidelines)

- **作用域隔离**：各组件逻辑封装在 `src/components/`，通过 `IMessageHandler` 处理特定窗口消息。
- **UI 布局**：采用 `LayoutController` 管理弹簧状态，坐标计算需考虑 `m_dpiScale`。
- **渲染管线**：`RenderEngine::DrawCapsule` 为入口，根据 `RenderContext` 的 `mode` 分发。
- **异步安全**：严禁在 UI 线程执行阻塞式网络请求，插件必须在独立线程异步拉取数据。

## 关键文件 (Important Files)

- `src/main.cpp`: Entry point.
- `src/DynamicIsland.cpp`: Main window logic and state machine.
- `src/RenderEngine.cpp`: Direct2D drawing logic and expanded weather view.
- `src/WeatherPlugin.cpp`: QWeather API integration (Now/Hourly/Indices). API Key 从 `x64/Release/config.ini` 的 `[Weather] APIKey` 读取，Host 使用企业自定义域名。
- `include/IslandState.h`: `RenderContext` and `IslandDisplayMode` definitions.
- `include/EventBus.h`: Thread-safe communication.
