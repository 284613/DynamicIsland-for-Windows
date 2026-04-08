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
- `/v7/indices/1d` → 生活指数建议
- 天气展开面板（400×180）：左侧实时天气 + 右侧 2×3 逐小时网格（时间/温度/描述）

**`HourlyForecast` 结构体**（`PluginManager.h` / `IslandState.h`）：
```cpp
struct HourlyForecast { std::wstring time, icon, text; float temp; };
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

## 📋 下一步规划：未来几天天气预报功能

### 目标

点击天气展开面板时，除逐小时预报外，支持查看**未来 3~7 天逐日预报**（日期 / 最高最低温 / 天气图标）。

### API

```
GET /v7/weather/7d?location={locationId}&key={apiKey}
```
字段：`daily[].fxDate`（日期）、`daily[].tempMax`、`daily[].tempMin`、`daily[].iconDay`、`daily[].textDay`

### 数据层

1. **`PluginManager.h`**：新增结构体
   ```cpp
   struct DailyForecast {
       std::wstring date;     // "MM-DD"
       std::wstring iconDay;  // 图标 ID
       std::wstring textDay;  // 如 "晴"
       float tempMax;
       float tempMin;
   };
   ```
2. **`IWeatherPlugin`**：添加 `virtual std::vector<DailyForecast> GetDailyForecast() const = 0;`
3. **`WeatherPlugin.h/.cpp`**：
   - 成员 `std::vector<DailyForecast> m_dailyForecasts`
   - `FetchWeather()` 末尾追加 `/v7/weather/7d` 请求，解析 `daily[0..6]`
4. **`IslandState.h`**：`RenderContext` 新增 `std::vector<DailyForecast> dailyForecasts`（结构体同上）
5. **`DynamicIsland.cpp`**：`BuildRenderContext()` 中复制 `dailyForecasts`

### 显示模式（二选一，用 `weatherViewMode` 枚举切换）

#### 模式 A — 副岛（Secondary Island）
- 天气图标展开时，主岛下方弹出副岛（已有副岛机制可复用）
- 副岛显示 7 条逐日预报，横向排列，每格：图标 + 日期 + 最高/最低温
- 上下键 / 鼠标滚轮 切换焦点行高亮

#### 模式 B — 展开面板内切换视图
- 天气展开（400×180）面板内新增视图页：**逐小时** ↔ **逐日**
- 鼠标滚轮或上下键在两个视图间切换（淡入淡出动画）
- 逐日视图：7 列网格，每列显示日期 / 天气图标（矢量）/ 最高温 / 最低温

### 渲染

- **`RenderEngine.cpp`**：新增 `DrawWeatherDaily()` 方法，布局与 `DrawWeatherExpanded()` 的右侧网格对齐风格一致
- 天气图标复用已有 `DrawWeatherIcon()` 或按 `iconDay` ID 映射到 `WeatherType`
- 切换动画：`contentAlpha` 淡出 → 更新 `weatherViewMode` → 淡入

### 状态

- `IslandState.h` 新增 `enum class WeatherViewMode { Hourly, Daily };`
- `RenderContext` 新增 `WeatherViewMode weatherViewMode`
- `DynamicIsland.cpp` 处理 `WM_KEYDOWN`（↑↓）和 `WM_MOUSEWHEEL` 时切换 `weatherViewMode`