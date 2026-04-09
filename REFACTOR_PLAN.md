# DynamicIsland 组件化重构计划

> 目标：将 `RenderEngine.cpp`（5684 行）拆解为多个独立组件，使新增/修改功能只需改对应组件文件，不触碰渲染引擎核心。

---

## 一、现状分析

### 核心问题

| 问题 | 具体表现 |
|------|----------|
| **RenderEngine.cpp 巨型文件** | 5684 行，所有功能绘制内联于 `DrawCapsule()` 一个函数 |
| **DrawCapsule 是巨型 if-else 链** | 从第 984 行到 3310 行，按优先级判断画什么，嵌套深达 5 层 |
| **RenderContext 大包大揽** | 100 行 struct，包含音乐/天气/音量/文件/通知所有字段，改任何功能都要改它 |
| **天气绘制最重** | `DrawWeatherAmbientBg`(~360行) + `DrawWeatherExpanded`(~120行) + `DrawWeatherDaily`(~90行) + `DrawWeatherIcon`(~480行) 合计约 2500 行散布在引擎末尾 |
| **已有组件接口不统一** | `MusicPlayerComponent` 需要手动调 `SetD2DResources()` + `SetBrushes()` + `SetTextFormats()` 传入大量参数 |
| **状态耦合** | `m_weatherType`、`m_weatherAnimPhase`、`m_lyricScrollOffset` 等功能私有状态直接挂在 `RenderEngine` 成员变量上 |
| **数据流混乱** | `DynamicIsland::UpdatePhysics()` 手动填 `RenderContext` → `RenderEngine` 再读，中间层无意义 |

### 当前 DrawCapsule 主干逻辑

```
DrawCapsule()
├── if (WeatherExpanded)          → DrawWeatherDaily / DrawWeatherExpanded
├── else if (isAlertActive)       → 通知/连接紧凑卡片 或 展开详情卡片
├── else if (isVolumeActive)      → 主岛音量条 (compact 模式)
├── else if (isDragHovering)      → 文件拖拽提示图标+文字
├── else if (storedFileCount > 0) → 暂存文件 compact/展开 显示
├── else if (compactMode)
│   ├── 左：时间
│   ├── 右：天气小图标+温度 / 播放波形
│   └── 中：空
└── else (展开模式)
    ├── if (hasSession && isPlaying) → 专辑封面+歌词+歌名+进度条+播放按钮+波形
    └── else → 空白
副岛：secondaryHeight > 0 → 音量副岛（图标+进度条）
```

---

## 二、目标架构

```
┌─────────────────────────────────────────────────────┐
│                    RenderEngine                     │
│  职责：D3D/D2D/DComp 初始化 · 胶囊壳绘制 · 组件分发  │
│  目标行数：~600 行                                   │
└──────────────────────┬──────────────────────────────┘
                       │ Draw(rect, alpha, time)
        ┌──────────────┼──────────────────────┐
        ▼              ▼                      ▼
 ┌────────────┐  ┌────────────┐  ┌──────────────────┐
 │WeatherComp │  │MusicComp   │  │AlertComponent    │
 │(天气展开+  │  │(封面+歌词+ │  │(WiFi/蓝牙/充电/  │
 │ 意境动画)  │  │ 进度+按钮) │  │  通知/低电)      │
 └────────────┘  └────────────┘  └──────────────────┘
        ▼              ▼                      ▼
 ┌────────────┐  ┌────────────┐  ┌──────────────────┐
 │VolumeComp  │  │FileStorage │  │ClockComponent    │
 │(主岛+副岛  │  │Component   │  │(时间显示)        │
 │  音量条)   │  │(暂存文件)  │  └──────────────────┘
 └────────────┘  └────────────┘
        ▼              ▼
 ┌────────────┐  ┌────────────┐
 │LyricsComp  │  │WaveformComp│
 │(歌词滚动+  │  │(三柱音频   │
 │  渐变遮罩) │  │  波形动画) │
 └────────────┘  └────────────┘
```

数据流：
```
Monitor/Plugin → EventBus → 各组件订阅自己需要的事件
RenderContext  → 只保留布局信息（width/height/alpha/mode）
```

---

## 三、统一组件接口

新建 `include/components/IIslandComponent.h`：

```cpp
#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>

// 传给每个组件的共享 D2D 资源（引擎初始化后创建，所有组件共用）
struct SharedResources {
    ID2D1DeviceContext*  d2dContext    = nullptr;
    IDWriteFactory*      dwriteFactory = nullptr;
    ID2D1Factory1*       d2dFactory    = nullptr;

    // 公共画刷（组件直接用，不自己创建）
    ID2D1SolidColorBrush* whiteBrush    = nullptr;
    ID2D1SolidColorBrush* grayBrush     = nullptr;
    ID2D1SolidColorBrush* blackBrush    = nullptr;
    ID2D1SolidColorBrush* themeBrush    = nullptr;

    // 公共字体
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* subFormat   = nullptr;
    IDWriteTextFormat* iconFormat  = nullptr;

    // TextLayout 缓存（共享，避免重复创建）
    // 使用 RenderEngine 内现有的 GetOrCreateTextLayout
};

class IIslandComponent {
public:
    virtual ~IIslandComponent() = default;

    // ── 生命周期 ──────────────────────────────────────────
    // 引擎 Initialize() 完成后调用一次，组件在此创建私有资源
    virtual void OnAttach(SharedResources* res) = 0;

    // 窗口 DPI/尺寸变化时调用，组件重建依赖像素的资源
    virtual void OnResize(float dpi, int width, int height) {}

    // ── 每帧调用 ─────────────────────────────────────────
    // 推进动画状态（滚动偏移/物理/相位），不绘制
    virtual void Update(float deltaTime) {}

    // 在给定矩形内绘制；alpha 已由引擎计算好
    virtual void Draw(const D2D1_RECT_F& rect,
                      float contentAlpha,
                      ULONGLONG currentTimeMs) = 0;

    // ── 查询 ─────────────────────────────────────────────
    // 当前组件是否处于激活状态（决定优先级调度）
    virtual bool IsActive() const { return false; }

    // 是否需要持续渲染（动画进行中，阻止引擎进入空闲）
    virtual bool NeedsRender() const { return false; }

    // ── 输入（可选重写）──────────────────────────────────
    // 返回 true 表示已消费，引擎不再向下传递
    virtual bool OnMouseWheel(float x, float y, int delta) { return false; }
    virtual bool OnMouseMove(float x, float y)             { return false; }
    virtual bool OnMouseClick(float x, float y)            { return false; }
};
```

**与旧接口的关键区别：**
- 不再需要 `SetBrushes()` / `SetTextFormats()` / `SetD2DResources()` 三连击
- `IsActive()` 让组件自己知道自己该不该显示，引擎只需遍历优先级表
- `Update()` 与 `Draw()` 分离，状态推进和绘制职责明确

---

## 四、共享资源层

新建 `include/SharedResources.h`，在 `RenderEngine::Initialize()` 中填充，通过 `OnAttach` 传给每个组件。

组件**可以直接使用**共享资源中的公共画刷/字体，**也可以在 `OnAttach` 内自行创建**私有资源（如天气的渐变画刷、进度条专用画刷）。

---

## 五、组件拆分详细说明

### 5.1 WeatherComponent（最大收益）

**拆出内容（当前 ~2500 行）**

| 子文件 | 职责 | 搬自 RenderEngine.cpp |
|--------|------|----------------------|
| `WeatherComponent.h/cpp` | 组件主体：持有天气数据和视图状态、滚轮切换、`IsActive()`、`NeedsRender()` | `m_weatherType` `m_weatherAnimPhase` `m_lastWeatherAnimTime`，以及 `ctx.weatherDesc/temp/iconId` 等字段 |
| `WeatherRenderer.h/cpp` | 绘制展开面板（左卡意境背景+右卡网格）、逐日视图 | `DrawWeatherExpanded()` L5562–5684、`DrawWeatherDaily()` L5111–5199 |
| `WeatherAnimations.h/cpp` | 8 种意境背景动画（雨/雷/雪/晴昼/晴夜/多云/晴间多云/雾） | `DrawWeatherAmbientBg()` L5200–5561 |
| `WeatherIconRenderer.h/cpp` | 矢量天气图标绘制（用于 compact 小图标和面板内大图标） | `DrawWeatherIcon()` L4627–5110、`MapWeatherDescToType()` |

**组件私有状态（从 RenderEngine 迁移过来）：**
```cpp
class WeatherComponent : public IIslandComponent {
    WeatherViewMode  m_viewMode     = WeatherViewMode::Hourly;
    WeatherType      m_weatherType  = WeatherType::Default;
    float            m_animPhase    = 0.0f;
    ULONGLONG        m_lastAnimTime = 0;
    std::wstring     m_desc;
    float            m_temp         = 0.0f;
    std::wstring     m_iconId;
    std::vector<HourlyForecast> m_hourly;
    std::vector<DailyForecast>  m_daily;
    bool             m_isExpanded   = false;   // 从 EventBus 订阅
};
```

**数据来源：** 订阅 EventBus 上的 `WeatherUpdatedEvent`，不再依赖 `RenderContext`。

**compact 天气图标（右侧温度+小图标）：**
compact 状态下的天气显示也归 WeatherComponent 管，Draw() 根据 rect 大小自动判断是画小图标还是展开面板。

---

### 5.2 MusicPlayerComponent（改造已有）

**当前状态：** `src/components/MusicPlayerComponent.cpp`（472 行），但 `DrawCapsule` 展开模式音乐绘制（~800 行）仍在 RenderEngine 内联，两者重复。

**重构后：** 将 RenderEngine 内的音乐展开绘制全部移入组件，并实现 `IIslandComponent`。

**负责内容：**
- 专辑封面（位图绘制 + 占位色块）
- 歌名+歌手文本（省略号+左对齐）
- 进度条（背景槽 + 前景条 + 悬停放大动画）
- 播放控制按钮（调用 WaveformComponent 和 `DrawPlaybackButtons`）
- compact 模式：滚动歌名文本（循环滚动逻辑从引擎搬入）

**组件私有状态（从 RenderEngine 迁移）：**
```cpp
float m_titleScrollOffset = 0.0f;
float m_artistScrollOffset = 0.0f;
bool  m_titleScrolling     = false;
std::wstring m_lastTitle, m_lastArtist;
ComPtr<ID2D1Bitmap> m_albumBitmap;
```

---

### 5.3 LyricsComponent（新建）

**职责：** 专门负责歌词滚动显示，从 MusicPlayerComponent 进一步解耦。

**负责内容：**
- 当前歌词文本渲染（循环滚动）
- 渐变遮罩（左右边缘淡出，当前已注释，重构后启用）
- `UpdateScroll()` 逻辑（弹簧速度、偏移量推进）

**组件私有状态（从 RenderEngine 迁移）：**
```cpp
float        m_scrollOffset   = 0.0f;
float        m_scrollVelocity = 0.0f;
bool         m_isScrolling    = false;
std::wstring m_lastLyric;
ComPtr<ID2D1LinearGradientBrush> m_fadeLeft, m_fadeRight;
```

**接口调整：**
`RenderEngine::UpdateScroll()` 方法直接废弃，`LyricsComponent::Update(dt)` 内部处理。

---

### 5.4 WaveformComponent（新建）

**职责：** 三柱音频波形动画，当前散落在 compact 和展开两处共约 50 行重复代码。

**负责内容：**
- 三柱矩形高度的随机目标更新
- 弹簧插值平滑
- 绘制三柱色块

**组件私有状态（从 RenderEngine 迁移）：**
```cpp
float    m_currentHeight[3] = {10,10,10};
float    m_targetHeight[3]  = {10,10,10};
float    m_phase[3]         = {0.0f, 1.2f, 2.4f};
ULONGLONG m_lastUpdate      = 0;
```

---

### 5.5 AlertComponent（改造已有）

**当前状态：** `src/components/AlertComponent.cpp`（204 行），但 RenderEngine 内仍有大量 Alert 绘制代码（L1072–1545，约 470 行）。

**重构后：** 将 RenderEngine 内的 Alert 绘制全部移入组件，实现 `IIslandComponent`。

**负责内容：**
- compact：图标（字符/位图）+ 单行文字
- expanded：左侧大图标/图片 + 右侧标题+描述
- 6 种类型分支：WiFi(1) / 蓝牙(2) / App通知(3) / 充电(4) / 低电量(5) / 文件(6)
- 通知位图加载（`LoadAlertIcon` / `LoadAlertIconFromMemory` 从引擎搬入组件）

**组件私有画刷（从 RenderEngine 迁移）：**
```cpp
ComPtr<ID2D1SolidColorBrush> m_wifiBrush;
ComPtr<ID2D1SolidColorBrush> m_bluetoothBrush;
ComPtr<ID2D1SolidColorBrush> m_chargingBrush;
ComPtr<ID2D1SolidColorBrush> m_lowBatteryBrush;
ComPtr<ID2D1SolidColorBrush> m_fileBrush;
ComPtr<ID2D1SolidColorBrush> m_notificationBrush;
ComPtr<ID2D1Bitmap>          m_alertBitmap;
```

---

### 5.6 VolumeComponent（改造已有）

**当前状态：** `src/components/VolumeComponent.cpp`（74 行），RenderEngine 内仍有音量绘制（主岛 compact 音量条 ~120 行 + 副岛音量 ~80 行）。

**重构后：** 合并主岛和副岛绘制，实现 `IIslandComponent`。

**负责内容：**
- 主岛 compact：扬声器图标 + 音量进度条
- 副岛（secondaryHeight > 0）：独立胶囊 + 小图标 + 条
- 音量图标按等级选择（0%/1-35%/36-65%/66-100%）

**说明：** 副岛的背景（黑色圆角矩形）由引擎负责，VolumeComponent 只负责内容。

---

### 5.7 FileStorageComponent（新建）

**职责：** 文件暂存功能，从 `DrawCapsule` 中拆出（L1674–1965，约 290 行）。

**负责内容：**
- 拖拽悬停提示（箭头图标 + "松开以暂存文件"）
- compact：文件夹图标 + "已暂存 N 个文件"
- expanded：大图标 + 文件列表 + "点击全部打开"
- 悬停删除按钮交互

**IsActive() 条件：**
```cpp
bool IsActive() const override {
    return m_isDragHovering || m_storedFileCount > 0;
}
```

---

### 5.8 ClockComponent（新建）

**职责：** compact 模式左侧时间显示（L1978–2040，约 60 行）。

**负责内容：**
- 时间文本居中绘制
- `IsActive()` 只在 compact 且无音乐播放时返回 true（最低优先级）

**说明：** 这是最简单的组件，但拆出后引擎再也不需要知道时间格式化逻辑。

---

## 六、RenderContext 瘦身

**重构前（100 行，所有功能数据混在一起）：**
```cpp
struct RenderContext {
    // 布局
    float islandWidth, islandHeight, canvasWidth, contentAlpha;
    float secondaryHeight, secondaryAlpha;
    IslandDisplayMode mode;
    // 音乐（9 个字段）
    // 时间（2 个字段）
    // 进度条（3 个字段）
    // 歌词（1 个字段）
    // 音量（2 个字段）
    // 文件（5 个字段）
    // 天气（8 个字段 + 2 个 vector）
    // 通知（4 个字段）
};
```

**重构后（只保留布局信息，~20 行）：**
```cpp
struct RenderContext {
    float             islandWidth;
    float             islandHeight;
    float             canvasWidth;
    float             contentAlpha;
    float             secondaryHeight;   // 副岛动画高度
    float             secondaryAlpha;    // 副岛透明度
    IslandDisplayMode mode;              // 当前优先显示的模式
    ULONGLONG         currentTimeMs;     // 当前帧时间戳
};
```

各功能数据由对应组件通过 EventBus 订阅维护，不再经过 RenderContext 中转。

---

## 七、优先级调度表（替换 if-else 链）

**重构前：** DrawCapsule 内嵌套 if-else 约 2300 行。

**重构后：**

```cpp
// RenderEngine.cpp，初始化时注册
void RenderEngine::RegisterComponents() {
    // 按优先级从高到低
    m_componentStack = {
        { IslandDisplayMode::WeatherExpanded, m_weatherComp.get()     },
        { IslandDisplayMode::Alert,           m_alertComp.get()        },
        { IslandDisplayMode::Volume,          m_volumeComp.get()       },
        { IslandDisplayMode::FileDrop,        m_fileStorageComp.get()  },
        { IslandDisplayMode::MusicExpanded,   m_musicComp.get()        },
        { IslandDisplayMode::MusicCompact,    m_musicComp.get()        },
        { IslandDisplayMode::Idle,            m_clockComp.get()        },
    };
}

// DrawCapsule 主干
void RenderEngine::DrawCapsule(const RenderContext& ctx) {
    BeginFrame();
    DrawBackground(ctx);           // 胶囊壳+圆角（保留在引擎）

    for (auto& [mode, comp] : m_componentStack) {
        if (comp->IsActive()) {
            comp->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
            break;
        }
    }

    DrawSecondaryIsland(ctx);      // 副岛壳（引擎画壳，内容委托 VolumeComponent）
    EndFrame();
}
```

---

## 八、文件结构变化

**新增文件：**
```
include/components/
  IIslandComponent.h          ← 统一接口
  SharedResources.h           ← 共享 D2D 资源结构体
  WeatherComponent.h
  WeatherRenderer.h
  WeatherAnimations.h
  WeatherIconRenderer.h
  LyricsComponent.h
  WaveformComponent.h
  FileStorageComponent.h
  ClockComponent.h

src/components/
  WeatherComponent.cpp
  WeatherRenderer.cpp
  WeatherAnimations.cpp
  WeatherIconRenderer.cpp
  LyricsComponent.cpp
  WaveformComponent.cpp
  FileStorageComponent.cpp
  ClockComponent.cpp
```

**改造（不新建）：**
```
src/components/MusicPlayerComponent.cpp  ← 实现 IIslandComponent，吸收引擎内音乐绘制
src/components/AlertComponent.cpp        ← 实现 IIslandComponent，吸收引擎内通知绘制
src/components/VolumeComponent.cpp       ← 实现 IIslandComponent，吸收主岛+副岛音量
```

**大幅缩减：**
```
src/RenderEngine.cpp          5684 行 → ~600 行
include/RenderEngine.h        194 行  → ~80 行
include/IslandState.h         100 行  → ~25 行（RenderContext 瘦身）
```

---

## 九、执行顺序（6 个 PR）

| PR | 状态 | 关键变更 |
|----|------|---------|
| PR1 | ✅ **已完成** | 新建 `IIslandComponent` + `SharedResources`；`RegisterComponents()` 骨架 |
| PR2 | ✅ **已完成** | `WeatherComponent` 接管天气全部绘制；意境背景动画每次展开重置 |
| PR3 | ✅ **已完成** | `LyricsComponent`（弹簧滚动）/ `WaveformComponent`（三柱动画）独立 |
| PR4 | ✅ **已完成** | `AlertComponent` / `VolumeComponent` / `MusicPlayerComponent` 实现 `IIslandComponent`；`SharedResources` 扩展全部画刷 + WIC；旧内联代码以 `if(false)` 保留作 fallback |
| PR5 | ✅ **已完成** | 新建 `ClockComponent`（时钟居中显示）；重构 `FilePanelComponent` 实现 `IIslandComponent` |
| PR6 | ✅ **已完成** | 优先级调度表替换 if-else 链；`RenderContext` 瘦身（移除10个死字段）；EventBus 清理（移除10个未用事件类型） |

### PR1 — 基础设施（✅ 已完成）
- 新建 `IIslandComponent.h`（含 `SharedResources` struct）
- `RenderEngine` 中添加 `RegisterComponents()` 骨架，行为不变

### PR2 — 拆天气组件（✅ 已完成）
- 新建 `WeatherComponent`（单文件，含全部天气逻辑）
- `DrawCapsule` 天气分支委托给组件；`SetExpanded(true)` 每次重置动画
- compact 天气小图标也委托给 `DrawCompact()`

### PR3 — 拆歌词和波形（✅ 已完成）
- 新建 `LyricsComponent`（弹簧物理滚动 + 渐变遮罩）
- 新建 `WaveformComponent`（随机目标高度 + 弹簧插值）
- `UpdateScroll()` 末尾桥接两个组件

### PR4 — 改造 Music / Alert / Volume（✅ 已完成）
- 三个已有组件全部实现 `IIslandComponent`
- `OnAttach(SharedResources*)` 替换 `SetBrushes / SetTextFormats / SetD2DResources`
- `SharedResources` 扩展：提示画刷（wifi/bt/charging/…）、音乐画刷（progressBg/Fg/buttonHover）、WIC
- `DrawCapsule` alert/volume/expanded-music 分支委托给组件；旧内联代码保留为 `if(false)` fallback
- 波形数据通过 `SetWaveHeights()` 传入；按钮 hover/press 效果恢复

### PR5 — 新建 FileStorage / Clock（✅ 已完成）
- 新建 `ClockComponent`（时钟居中显示）
- 重构 `FilePanelComponent` 实现 `IIslandComponent` 接口（拖拽提示 + compact/展开文件列表）
- 组件已注册到 `RegisterComponents()`，旧内联代码保留在 `DrawCapsule`（待 PR6 迁移）

### PR6 — 收尾（✅ 已完成）
- `DetermineDisplayMode()` if-else 链替换为 `DisplayModePriorityTable`（可配置优先级调度表）
- `RenderContext` 删除 10 个死字段（`storedFiles`/`hoveredFileIndex`/`isFileDeleteHovered`/`weatherSuggestion`/`weatherHasWarning`/`isAlertActive`/`alertType`/`alertName`/`alertDeviceType`）
- `EventBus` 清理：移除 10 个未发布事件类型（`PowerChange`/`MediaProgressChanged`/`SystemTimeChanged`/`VolumeChanged`/`WindowStateChanged`/`HoverStateChanged`/`DragStateChanged`/`NotificationRemoved`/`NetworkStatusChanged`/`BluetoothStatusChanged`）及 3 个未用便捷发布方法

---

## 十、预期指标

| 指标 | 重构前 | 重构后 |
|------|--------|--------|
| RenderEngine.cpp | 5684 行 | ~600 行 |
| DrawCapsule() | ~2300 行 | ~60 行 |
| RenderContext | 100 行 / 30+ 字段 | ~25 行 / 7 字段 |
| 新增天气动画需改文件 | RenderEngine + DynamicIsland + IslandState | 只改 WeatherAnimations.cpp |
| 新增通知类型需改文件 | RenderEngine | 只改 AlertComponent.cpp |
| 新增独立功能 | 改 RenderContext → 改 UpdatePhysics → 改 DrawCapsule | 写组件 → RegisterComponents() 注册 |
| 组件可独立编译测试 | 不可能 | 可以 |

---

## 十一、重构注意事项 (Lessons Learned)

为了确保重构过程的高可用性和系统稳定性，必须严格遵守以下准则：

1.  **高频编译验证**：每新增一个文件、每修改一处逻辑、每更新一次 `vcxproj`，必须立即执行 `msbuild`。严禁在未验证编译通过的情况下进行连续修改。
2.  **保守删除原则**：在 `DrawCapsule` 的 `if-else` 链迁移过程中，采用“先注入、后委托、最后删除”的策略。在新组件未经过全场景验证前，禁止从 `RenderEngine.cpp` 中大段删除旧逻辑。
3.  **功能逻辑守恒**：重构期间必须保证 `DrawCapsule` 的分支完整性。如果某个分支尚未组件化，其原有代码必须完整保留在 `else` 分支中，严禁出现功能真空期。
4.  **慎用全量重写**：针对 `RenderEngine.cpp` 等超大型文件，优先使用外科手术式的 `replace` 修改。如必须使用 `write_file`，须反复核对函数闭合、宏定义（如 `min/max` 冲突）及变量作用域。
5.  **类型与命名空间安全**：
    *   警惕结构体重名（如 `RenderContext::HourlyForecast` 与全局 `::HourlyForecast`）。
    *   在组件中使用 `(std::min)` 或 `#undef min` 以规避 `windows.h` 的宏干扰。
6.  **窗口 Region 联动**：若组件涉及尺寸动态变化（如副岛弹出），必须确保在 `UpdatePhysics` 中同步触发 `UpdateWindowRegion`，否则内容会被系统裁剪。

---

## 十二、已知优化点

| # | 描述 | 涉及文件 | 优先级 |
|---|------|----------|--------|
| 1 | **天气面板每次打开都重置动画**：`SetExpanded(true)` 时调用了 `ResetAnimation()`，导致意境背景每次展开都从头播放入场动画（云朵重新飘入、相位归零）。应改为：仅在天气类型切换时重置，面板重新展开时保留当前 phase 继续播放。修改位置：`WeatherComponent::SetExpanded()` 中的 `ResetAnimation()` 调用条件。 | `src/components/WeatherComponent.cpp` | 中 |

---

*生成时间：2026-04-09*
