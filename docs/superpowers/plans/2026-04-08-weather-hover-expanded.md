# 计划：实现天气 Hover 动画与左右分栏的展开详情面板

## 1. 数据扩充 (WeatherPlugin)
- **目标**：接入 QWeather 的逐小时预报接口（24h）和生活指数接口（1d）。
- **修改 `include/WeatherPlugin.h`**：
  - 新增 `HourlyForecast` 结构体（包含时间、图标、温度）。
  - 新增 `LifeIndex` 结构体（包含自然语言建议）。
  - 在 `IWeatherPlugin` 接口或 `WeatherPlugin` 类中新增相应的 Getter 方法。
- **修改 `src/WeatherPlugin.cpp`**：
  - 在 `FetchWeather` 线程中，除了请求 `/v7/weather/now`，并行或顺序请求 `/v7/weather/24h` 和 `/v7/indices/1d?type=1,3,5`。
  - 使用现有的 JSON 解析辅助函数提取未来 6 个小时的数据和 1 条生活指数建议。

## 2. 交互响应 (DynamicIsland)
- **目标**：实现 Hover 动画与 Click 展开。
- **修改 `include/IslandState.h`**：
  - 在 `IslandDisplayMode` 中新增 `WeatherExpanded` 模式。
- **修改 `src/DynamicIsland.cpp`**：
  - **Hover**：在 `WM_MOUSEMOVE` 的天气图标检测逻辑中，调用 `m_renderer.TriggerWeatherAnimOnce()` 并通过 `Invalidate(Dirty_Hover)` 标脏。
  - **Click**：在 `WM_LBUTTONDOWN` 中，若点击区域在天气图标内，改变状态进入 `IslandDisplayMode::WeatherExpanded`，触发弹簧动画展开面板。

## 3. 渲染规范 (RenderEngine / WeatherComponent)
- **目标**：使用 Direct2D 绘制左右分栏的玻璃拟态面板。
- **新增/修改 `src/components/WeatherComponent.cpp` (或 `RenderEngine.cpp` 中新增渲染方法)**：
  - 绘制背景：使用深色、半透明（Alpha 0.2~0.4）的圆角矩形区分左右区域。
  - **左半部分 (Hero Section)**：大字号当前温度，当前天气图标，底部居中/对齐绘制自然语言生活建议。
  - **右半部分 (Hourly Forecast)**：绘制 2x3 网格的逐小时预报（时间、图标、温度）。纯白文字，恶劣天气使用高对比度亮色（如红色/橙色）。
  - 集成到 `RenderEngine::DrawCapsule` 的渲染管线中，根据 `RenderContext.mode == WeatherExpanded` 进行调用。