# 计划：设置应用修复 + 番茄时钟功能

## 执行状态

- 状态：已完成主要实现，当前进入视觉细化阶段
- 最近验证：`msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64` 通过
- 当前剩余：番茄时钟展开面板的按钮图标、间距、行对齐继续打磨

## Context

两个问题需要解决：
1. **设置保存不生效**：`SaveSettings()` 写入了 13+ 项配置到 config.ini，但 `WM_SETTINGS_APPLY` 处理器只回流了 3 项（AllowedApps、LowBatteryThreshold、Weather）。大量设置如 DarkMode、MainUI 尺寸、SpringStiffness 等写入后从未被重新读取。此外 `WeatherPlugin::LoadConfig()` 有 `static s_loaded` 一次性锁，导致天气 API Key 修改后也无法生效。
2. **番茄时钟**：在空闲态主岛时钟区域点击后展开番茄时钟 UI，支持设置倒计时、暂停、终止、收缩/展开。

---

## Part 1：修复设置应用

### 根因分析

| 位置 | 问题 |
|------|------|
| `DynamicIsland.cpp:754-763` | `WM_SETTINGS_APPLY` 中 `LoadConfig()` 只读 CanvasWidth/Height/AllowedApps |
| `WeatherPlugin.cpp:21-23` | `static s_loaded` 阻止二次加载 API Key/LocationId |
| `LayoutController` | SpringStiffness/Damping 硬编码，未从 config 读取 |

### 修改方案

**文件：`src/DynamicIsland.cpp` — `WM_SETTINGS_APPLY` 处理器（约 line 754）**

扩展 apply 处理，补充回流以下设置项：
```
- DarkMode / FollowSystemTheme → 通知 RenderEngine 切换主题色
- MainUI Width/Height → 更新 LayoutController 的基准尺寸
- MainUI Transparency → 更新 RenderEngine 透明度
- SpringStiffness/Damping → 传给 LayoutController
- FileStashMaxItems → 传给 FileStashStore
- MediaPollIntervalMs → 传给 MediaMonitor
- AutoStart → 更新注册表启动项
```

**文件：`src/WeatherPlugin.cpp` — `LoadConfig()` (line 20-51)**

- 去掉 `static s_loaded` 一次性锁，或新增 `ReloadConfig()` 方法跳过该锁
- 确保 `UpdateWeather()` 调用前能拿到最新 API Key 和 LocationId

**文件：`src/LayoutController.cpp`**

- 新增 `SetSpringParams(float stiffness, float damping)` 方法替代硬编码值

### 验证

- 修改设置窗口中任一项 → 点击"保存并应用" → 不重启，观察行为变化
- 修改天气 API Key → 保存应用 → 检查天气是否刷新
- 修改 Spring 参数 → 保存应用 → 观察动画弹性变化

### 执行结果

- `WM_SETTINGS_APPLY` 已扩展为重新读取并应用主题、透明度、主岛展开尺寸、弹簧参数、文件暂存上限、媒体轮询间隔、开机自启
- `WeatherPlugin` 已去掉一次性加载锁，天气配置支持保存后刷新
- `LayoutController` 已提供 `SetSpringParams(float stiffness, float damping)`

---

## Part 2：番茄时钟功能

### 交互设计

**触发方式：** 空闲态主岛点击时钟文字区域

**展开态 (Expanded)：**
- 主岛展开为较大面板（类似音乐展开态尺寸）
- 上方：番茄时钟标题 + 环形进度条（圆环内显示剩余时间 mm:ss）
- 中间：预设时间选择（25min / 15min / 5min）或自定义输入
- 下方：操作按钮行 —— 开始 ▶ / 暂停 ⏸ / 终止 ■ / 收缩 ↑
- 按钮用圆角矩形 + 图标，hover 有微动画

**收缩/紧凑态 (Compact)：**
- 回到 collapsed 尺寸，但时钟文字替换为倒计时 `mm:ss`
- 文字颜色区分：正在计时=番茄红，已暂停=黄色
- 时钟文字左侧可显示小圆点脉冲动画表示运行中
- 右侧保留小按钮区域：暂停 ⏸ / 终止 ✕

**状态流：**
```
Idle(时钟) --点击--> PomodoroExpanded(设置面板)
  --开始--> PomodoroExpanded(运行中) --收缩--> PomodoroCompact(紧凑倒计时)
  PomodoroCompact --点击--> PomodoroExpanded(运行中/暂停中)
  --倒计时结束--> 提示弹窗(Alert) --> Idle
  --终止--> Idle
```

### 新增文件

**`include/components/PomodoroComponent.h`**
```cpp
class PomodoroComponent : public IIslandComponent {
public:
    // IIslandComponent 接口
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override;
    bool NeedsRender() const override;
    bool OnMouseClick(float x, float y) override;
    bool OnMouseMove(float x, float y) override;

    // 番茄时钟专用接口
    void SetExpanded(bool expanded);
    bool IsExpanded() const;
    bool IsRunning() const;
    bool IsPaused() const;
    int GetRemainingSeconds() const;

    // 供 DynamicIsland 查询，决定显示模式
    bool HasActiveSession() const;

private:
    enum class State { Idle, Setting, Running, Paused, Finished };
    State m_state = State::Idle;
    bool m_expanded = false;
    int m_totalSeconds = 25 * 60;
    int m_remainingSeconds = 0;
    ULONGLONG m_lastTickMs = 0;

    // UI 状态
    float m_ringProgress = 0.0f;   // 环形进度 0~1
    int m_hoveredButton = -1;      // 悬停按钮索引
    struct ButtonRect { D2D1_RECT_F rect; int id; };
    std::vector<ButtonRect> m_buttons;

    // 绘制方法
    void DrawExpanded(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawCompact(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawRing(float cx, float cy, float radius, float progress, float alpha);
    void DrawButton(const D2D1_RECT_F& r, const wchar_t* icon, bool hovered, float alpha);
};
```

**`src/components/PomodoroComponent.cpp`** — 完整实现

### 修改现有文件

**`include/IslandState.h`**
- `IslandDisplayMode` 新增 `PomodoroExpanded`、`PomodoroCompact`

**`include/RenderEngine.h`**
- 新增 `std::unique_ptr<PomodoroComponent> m_pomodoroComponent;`

**`src/RenderEngine.cpp`**
- `RegisterComponents()`: 创建并注册 PomodoroComponent
- `ResolvePrimaryComponent()`: PomodoroExpanded/Compact → m_pomodoroComponent
- `DrawPrimaryContent()`: Idle 模式下将点击转发给 ClockComponent → PomodoroComponent

**`src/DynamicIsland.cpp`**
- 优先级表新增两个条目：
  ```
  { IslandDisplayMode::PomodoroExpanded, 80, [this]() { return m_pomodoroExpanded; } },
  { IslandDisplayMode::PomodoroCompact,  45, [this]() { return m_pomodoroComponent->HasActiveSession() && !m_pomodoroExpanded; } },
  ```
  - 优先级 80 在天气展开之下、音乐展开之上
  - 优先级 45 在音乐紧凑之下，确保音乐播放时音乐优先
- `LayoutController` 新增 PomodoroExpanded/Compact 的目标尺寸
- 倒计时结束时触发 Alert 弹窗通知

**`include/components/ClockComponent.h`**
- 新增 `OnMouseClick()` override，点击时通知 DynamicIsland 进入番茄时钟模式

### UI 细节

- **配色**：番茄红 `#E74C3C` 为主色，暂停黄 `#F39C12`，背景保持黑色
- **环形进度条**：D2D Arc geometry，2px 描边底圈 + 3px 进度圈，圆角端点
- **按钮样式**：圆角矩形 (radius 8px)，默认半透明白底，hover 时提亮，按下缩小 0.95x
- **预设时间按钮**：三个胶囊形按钮横排（25 / 15 / 5），选中态番茄红填充
- **紧凑态倒计时**：替换时钟文字位置，字号不变，左侧加 4px 圆点脉冲
- **收缩/展开过渡**：复用现有弹簧动画系统

### 验证

- 空闲态点击时钟 → 展开番茄设置面板
- 选择 25min → 点击开始 → 环形进度开始走动
- 点击收缩 → 回到紧凑态，显示倒计时 `mm:ss`
- 紧凑态点击 → 重新展开，可暂停/终止
- 暂停后文字变黄，恢复后变红
- 倒计时归零 → 弹出提示 → 自动回到 Idle
- 音乐播放时番茄紧凑态让位于音乐紧凑态（优先级正确）

### 执行结果

- 已新增 `PomodoroComponent` 并接入 `RenderEngine` 组件注册 / 路由
- `IslandDisplayMode` 已新增 `PomodoroExpanded`、`PomodoroCompact`
- `ClockComponent` 已可将点击转发为番茄时钟展开入口
- 番茄时钟结束后会通过现有 Alert 链路提示并回到 Idle
- `PomodoroTimer` 已修复计时累计逻辑，开始后可正常按秒递减

## 当前备注

- 功能通路已经打通，优先级、收缩/展开、倒计时与提醒均已接入
- 当前计划文件不再是待执行清单，后续如继续迭代，建议新开视觉 polishing 计划，避免把“已实现功能”和“界面细调”混在同一份执行文档中
