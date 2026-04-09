# CLAUDE.md - Dynamic Island for Windows

## 项目概览

将 iPhone 14 Pro 灵动岛移植到 Windows 10/11 的桌面应用。屏幕顶部胶囊形悬浮窗，集中展示音乐、天气、通知、音量等系统状态。

**技术栈**：C++17 / Win32 API / Direct2D 1.1 + DirectComposition + D3D11 / MSBuild VS2022

## 架构（三层）

```
输入层  MediaMonitor / NotificationMonitor / WeatherPlugin / SystemMonitor
处理层  TaskDetector → LayoutController（弹簧物理）→ DynamicIsland（主控）
渲染层  RenderEngine（Direct2D）+ src/components/（各功能组件）
```

**核心机制**：EventBus 事件总线 · 弹簧物理动画 · DirtyFlags 按需渲染（空闲 0% CPU）· Win32 Region 多岛合并

## 关键文件

| 文件 | 作用 |
|------|------|
| `src/DynamicIsland.cpp` | 主窗口逻辑、状态机、优先级调度 |
| `src/RenderEngine.cpp` | Direct2D 绘制，含天气展开面板 `DrawWeatherExpanded()` |
| `src/WeatherPlugin.cpp` | 和风天气 API（Now/Hourly/Daily/Indices）|
| `src/LayoutController.cpp` | 弹簧布局，坐标计算含 `m_dpiScale` |
| `include/IslandState.h` | `RenderContext` · `IslandDisplayMode` 定义 |
| `include/PluginManager.h` | `HourlyForecast` · `IWeatherPlugin` 接口 |
| `include/EventBus.h` | 线程安全发布/订阅 |

## 天气模块现状

**API**：和风天气企业版，Host `n94ewu37fy.re.qweatherapi.com`，Key 从 `x64/Release/config.ini` `[Weather] APIKey` 读取。

**已实现**：
- `/v7/weather/now` → 实时天气（`now.temp` / `now.icon` / `now.text`）
- `/v7/weather/24h` → 逐小时预报（`hourly[].fxTime` / `.temp` / `.icon` / `.text`）
- `/v7/weather/7d` → 逐日预报（`daily[].fxDate` / `.tempMax` / `.tempMin` / `.iconDay` / `.textDay`）
- `/v7/indices/1d` → 生活指数建议
- 天气展开面板（400×180）：
  - **左卡**：意境动态背景（`DrawWeatherAmbientBg`）+ 底部温度居中 + 天气描述
  - **右卡**：2×3 逐小时网格（时间 / 矢量小图标 / 温度）
  - **滚轮切换**：`WeatherViewMode::Hourly ↔ Daily`（`DrawWeatherDaily` 7列逐日网格）
- `ShouldKeepRendering()` 在 `m_isWeatherExpanded` 时持续渲染

**意境背景天气类型**（`DrawWeatherAmbientBg`）：

| 天气 | 动画 |
|------|------|
| 雨天 | 深蓝渐变 + 云聚顶 + 斜向雨滴循环 |
| 雷雨 | 同上 + 周期闪电爆闪 |
| 雪天 | 冷蓝夜空 + 云 + 旋转六角雪花飘落 |
| 晴（白天）| 蓝天 + 右上角太阳光晕 + 旋转光芒 |
| 晴（夜晚）| 深靛蓝 + 新月 + 12颗闪烁星 |
| 多云/阴 | 晴空一次性入场 → 云朵停住 + 风吹漂移 |
| 晴间多云 | 同多云，3朵云，太阳随遮盖淡出 |
| 雾/霾 | 浅灰 + 5层横向雾气正弦波动 |

**关键结构体**（`PluginManager.h` / `IslandState.h`）：
```cpp
struct HourlyForecast { std::wstring time, icon, text; float temp; };
struct DailyForecast  { std::wstring date, iconDay, textDay; float tempMax, tempMin; };
enum class WeatherViewMode { Hourly, Daily };
```

## 开发规范

- UI 线程禁止阻塞网络请求，天气 API 必须在独立线程（`std::thread::detach`）
- 新组件放 `src/components/`，实现 `IMessageHandler`
- 布局坐标乘 `m_dpiScale`
- 渲染入口 `RenderEngine::DrawCapsule` → 按 `RenderContext.mode` 分发

## Build

```
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
x64\Release\DynamicIsland.exe
```

---

## 📋 下一步规划

### 功能待办
- 天气展开面板切换动画（`contentAlpha` 淡出 → 切换视图 → 淡入）
- 逐日视图中矢量图标按 `iconDay` ID 精确映射（当前用 `textDay` 推断）
- 意境背景雨滴/雪花数量随天气强度（小雨/大雨/暴雨）动态调整

### 🔧 组件化重构（详见 `REFACTOR_PLAN.md`）

目标：将 `RenderEngine.cpp`（5684 行）拆解为独立组件，新增功能只改对应组件文件。

**执行顺序（6 个 PR）：**

| PR | 内容 | 状态 |
|----|------|------|
| PR1 | 新建 `IIslandComponent` 接口 + `SharedResources` + `RegisterComponents()` 骨架 | ✅ 完成 |
| PR2 | 拆天气组件：`WeatherComponent` / `WeatherRenderer` / `WeatherAnimations` / `WeatherIconRenderer` | ✅ 完成 |
| PR3 | 新建 `LyricsComponent` / `WaveformComponent`，废弃 `RenderEngine::UpdateScroll()` | ✅ 完成 |
| PR4 | 改造 `MusicPlayerComponent` / `AlertComponent` / `VolumeComponent` 实现新接口 | ✅ 完成 |
| PR5 | 新建 `FileStorageComponent` / `ClockComponent` | ⬜ 待开始 |
| PR6 | 优先级调度表替换 if-else 链，`RenderContext` 瘦身，切换 EventBus 数据流 | ⬜ 待开始 |

**预期结果：** `RenderEngine.cpp` 5684 行 → ~600 行，`DrawCapsule()` 2300 行 → ~60 行