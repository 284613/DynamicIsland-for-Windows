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

## Coding Style & Conventions

- **Language:** C++17.
- **Naming Conventions:**
  - Classes/Methods: `PascalCase`.
  - Member Variables: `m_` prefix with `camelCase` (e.g., `m_window`).
- **Indentation:** 4 spaces.
- **Includes:** 
  - Prefer `#include "components/xxx.h"` for component headers.
  - `include/` directory is in `AdditionalIncludeDirectories`.

## Important Files

- `src/main.cpp`: Entry point.
- `src/DynamicIsland.cpp`: Main window logic and state machine.
- `src/RenderEngine.cpp`: Direct2D drawing logic.
- `include/IslandState.h`: `RenderContext` definition.
- `include/EventBus.h`: Thread-safe communication.
