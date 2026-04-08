# CLAUDE.md - Dynamic Island for Windows

Project guide and instructions for AI assistants.

## 项目背景 (Project Background)

**Dynamic Island for Windows** 是一款旨在将 iPhone 14 Pro 的“灵动岛”交互体验移植到 Windows 10/11 系统的桌面应用。

- **核心价值**：在屏幕顶部提供一个美观、非侵入式的交互入口，集中展示系统状态（音乐、通知、音量、天气、文件传输等）。
- **目标用户**：追求桌面美化、高效多任务处理以及希望获得沉浸式 Windows 使用体验的用户。
- **当前状态**：项目已完成核心渲染引擎、弹簧物理动画、媒体监控、歌词同步、通知捕获、天气插件及文件暂存面板等核心功能。

## 技术架构 (Technical Architecture)

项目采用解耦的分层架构设计，确保监控模块与 UI 渲染的高度分离。

### 1. 架构分层
- **输入层 (Monitoring)**：负责捕获外部事件。包括 `MediaMonitor` (媒体会话)、`NotificationMonitor` (系统通知)、`WeatherPlugin` (天气数据)、`SystemMonitor` (电量/电源) 等。
- **处理层 (State Machine)**：负责逻辑判定与布局计算。
  - `TaskDetector`：聚合各监控模块状态，决定当前岛屿应显示的模式（Mode）。
  - `LayoutController`：处理基于弹簧物理 (`Spring.h`) 的平滑宽高过渡及碰撞检测。
  - `DynamicIsland`：主控类，管理窗口生命周期、消息循环及优先级调度。
- **渲染层 (Component System)**：基于组件化的渲染设计。
  - `RenderEngine`：Direct2D 核心绘制引擎，支持 `DirectComposition` 实现高性能透明窗口。
  - `Components/`：各个功能模块的绘制实现（如 `MusicPlayerComponent`, `AlertComponent`）。

### 2. 核心机制
- **事件总线 (EventBus)**：各模块间通过线程安全的发布/订阅模式进行通信，降低耦合度。
- **弹簧物理 (Spring Physics)**：所有 UI 缩放、移动、滚动（如歌词）均由弹簧算法驱动，提供流体感交互。
- **优先级调度 (Priority Queue)**：告警事件（Alert）按 P0-P3 优先级排队，高优先级事件（如低电量）可中断低优先级显示。
- **图形栈**：`Direct2D 1.1` + `DirectComposition` + `D3D11`，支持全硬件加速与 Per-Monitor DPI 感知。

## Build Commands

- **Build with MSBuild:** `msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64`
- **Build with Visual Studio:** Open `DynamicIsland.sln`, select **Release** and **x64**, then **Build Solution**.
- **Run Executable:** `x64\Release\DynamicIsland.exe`

## Testing

No automated tests currently exist in the codebase. Manual verification is required by running the application and observing UI behavior and system state updates.

## Project Structure

- `cpp/`: Core logic and window implementation.
- `h/`: Header files and interfaces.
- `Components/`: Direct2D UI rendering components (Alerts, Music Player, etc.).
- `docs/`: Design and optimization documentation.
- `vcxproj/`: Visual Studio project files.
- `DynamicIsland.sln`: Main Visual Studio solution.

## Coding Style & Conventions

- **Language:** C++17.
- **Frameworks:** Win32 API, Direct2D, WinRT (for Media/Notifications).
- **Naming Conventions:**
  - Classes/Structs: `PascalCase` (e.g., `DynamicIsland`, `RenderEngine`).
  - Methods/Functions: `PascalCase` (e.g., `Initialize`, `IsFullscreen`).
  - Member Variables: `m_` prefix followed by `camelCase` (e.g., `m_window`, `m_hInst`).
  - Global Constants: `g_` prefix or within a `Constants` namespace.
- **Indentation:** 4 spaces.
- **Comments:** Prefer Chinese for detailed explanations, but use English for variable/method names and brief technical notes.
- **Error Handling:** Use `HRESULT` checks and simple `bool` returns for initialization success.
- **State Management:** Uses a `RenderContext` and `IslandState` to decouple logic from rendering.
- **Events:** Communication between modules is handled via the `EventBus` (Pub/Sub pattern).

## Important Files

- `cpp/main.cpp`: Entry point, OLE/WinRT initialization.
- `cpp/DynamicIsland.cpp`: Main window logic and state machine.
- `cpp/RenderEngine.cpp`: Direct2D drawing logic.
- `cpp/LayoutController.cpp`: Spring-based animation and layout logic.
- `h/Messages.h`: Custom WM_USER messages.
- `h/Constants.h`: UI layout constants.
- `h/EventBus.h`: Thread-safe inter-module communication.
