# Dynamic Island 开发计划

> 创建日期: 2026-03-25
> 项目路径: E:\vs\c++\DynamicIsland

---

## 项目现状分析

### 已完成的核心功能

| 模块 | 状态 | 说明 |
|-----|------|-----|
| 窗口与渲染引擎 | ✅ 完成 | Direct2D 渲染、弹簧动画、DPI 缩放 |
| 媒体监控 | ✅ 完成 | Spotify/网易云音乐 检测、进度同步 |
| 专辑封面 | ✅ 完成 | 文件/内存加载、模糊背景 |
| 播放控制 | ✅ 完成 | 上一曲/播放暂停/下一曲按钮 |
| 进度条 | ✅ 完成 | 拖动定位、悬停高亮 |
| 歌词监控 | ✅ 完成 | 网易云歌词抓取、时间轴同步 |
| Alert 弹窗 | ✅ 完成 | WiFi/蓝牙/通知/低电量 |
| 音量控制 | ✅ 完成 | 滚轮调节、图标动态切换 |
| 系统监控 | ✅ 完成 | 电量/充电状态 |
| 文件暂存面板 | ✅ 完成 | 拖拽接收、文件列表管理 |
| 设置窗口 | ✅ 完成 | 主题色/透明度/尺寸配置 |
| 托盘图标 | ✅ 完成 | 右键菜单、退出功能 |
| 热键 | ✅ 完成 | Ctrl+Alt+I 展开岛屿 |

### 需要完善的模块

| 模块 | 状态 | 说明 |
|-----|------|-----|
| FunctionMenu | ⚠️ 部分完成 | 框架存在，功能入口为占位实现 |
| PomodoroTimer | ⚠️ 头文件存在 | WM_USER 消息框架，无实际倒计时逻辑 |
| IslandCarousel | ⚠️ 头文件存在 | 任务轮播框架，无实际轮播逻辑 |
| TaskDetector | ⚠️ 头文件存在 | 任务检测框架，无 .cpp 实现 |
| MusicPlayerComponent | ⚠️ 存在但未集成 | 有独立渲染类，但主渲染在 RenderEngine |
| PluginManager | ⚠️ 接口定义 | 仅接口声明，无插件加载机制 |
| LanTransferManager | ⚠️ 接口定义 | 仅头文件，无实现 |
| NetworkDiscovery | ⚠️ 接口定义 | 仅头文件，无实现 |

### 技术债务

1. **常量重复定义** - `Constants.h` 定义了 `Constants::Size` 命名空间，但 `DynamicIsland.cpp` 仍使用自己的 `MINI_WIDTH`、`EXPANDED_WIDTH` 等全局常量
2. **组件未完全解耦** - `MusicPlayerComponent` 存在但未集成到主渲染流程
3. **错误处理不足** - 多处缺少空指针检查和异常捕获
4. **内存管理** - `AlertInfo` 等结构体通过 `new/delete` 管理，存在泄漏风险

---

## 分阶段开发计划

---

## Phase 1: 核心功能完善 (P0 阻塞任务)

### 目标: 修复已知问题，完善核心体验

---

### 任务 1.1: 常量统一重构

**优先级: P0 (阻塞)**
**依赖: 无**
**预估工作量: 2 小时**

**问题描述:**
`DynamicIsland.cpp` 使用全局常量 `MINI_WIDTH`、`EXPANDED_WIDTH` 等，与 `Constants.h` 中定义的 `Constants::Size` 命名空间不同步，导致维护困难。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `DynamicIsland.cpp` | 删除局部常量定义，改用 `Constants::Size::MINI_WIDTH` 等 |
| `DynamicIsland.h` | 删除 `MINI_WIDTH = 80.0f` 等行内常量，改为引用 `Constants::Size` |
| `Constants.h` | 确认所有尺寸常量完整，如有缺失补充 |

**具体修改:**

1. `DynamicIsland.h` 第 20-21 行:
```cpp
// 删除
const float MINI_WIDTH = 80.0f;
const float MINI_HEIGHT = 28.0f;
```
改为使用 `Constants::Size::MINI_WIDTH`

2. `DynamicIsland.h` 第 59-71 行删除重复的 `CANVAS_WIDTH`、`EXPANDED_WIDTH` 等局部常量

3. `DynamicIsland.cpp` 构造函数初始化列表改为:
```cpp
m_currentWidth(Constants::Size::COLLAPSED_WIDTH),
m_currentHeight(Constants::Size::COLLAPSED_HEIGHT),
```

---

### 任务 1.2: PomodoroTimer 番茄钟实现

**优先级: P1**
**依赖: 任务 1.1 (常量重构)**
**预估工作量: 4 小时**

**问题描述:**
`PomodoroTimer.h` 仅定义了消息类型，无实际倒计时逻辑。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `PomodoroTimer.h` | 添加工作/休息时间、状态机、回调通知 |
| `DynamicIsland.cpp` | 集成 PomodoroTimer，响应 `WM_USER` 消息 |

**具体修改:**

1. `PomodoroTimer.h` 添加:
   - `m_workDuration` (默认 25 分钟)
   - `m_breakDuration` (默认 5 分钟)
   - `m_longBreakDuration` (默认 15 分钟)
   - `m_isRunning` 状态
   - `m_remainingSeconds` 倒计时
   - 定时器 `WM_USER` 消息触发递减

2. `DynamicIsland.cpp` 添加 PomodoroTimer 成员变量，HandleMessage 添加 `case WM_USER:`

---

### 任务 1.3: TaskDetector 实现

**优先级: P1**
**依赖: 任务 1.1**
**预估工作量: 3 小时**

**问题描述:**
`TaskDetector.h` 定义了接口但无 `.cpp` 实现。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `TaskDetector.cpp` (新建) | 实现 `GetActiveTasks()`、`CheckMusicTask()` 等 |
| `DynamicIsland.cpp` | 集成 TaskDetector 用于任务状态检测 |

**具体修改:**

1. 新建 `TaskDetector.cpp`:
```cpp
TaskDetector::TaskDetector() { ... }
std::vector<TaskInfo> TaskDetector::GetActiveTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.clear();
    CheckMusicTask();
    CheckNotificationTask();
    CheckLyricsTask();
    return m_tasks;
}
```

2. `DynamicIsland.cpp` 中创建 TaskDetector 实例

---

### 任务 1.4: IslandCarousel 任务轮播实现

**优先级: P1**
**依赖: 任务 1.3 (TaskDetector)**
**预估工作量: 4 小时**

**问题描述:**
`IslandCarousel.h` 存在但无 `.cpp` 实现，任务无法自动轮播。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `IslandCarousel.cpp` (新建) | 实现 `GetNextTask()` 轮播逻辑 |
| `DynamicIsland.cpp` | 集成轮播逻辑，根据活跃任务自动切换显示 |

**具体修改:**

1. 新建 `IslandCarousel.cpp`，实现:
   - `m_taskInterval` (默认 5 秒切换)
   - `m_currentTaskIndex` 当前任务索引
   - `GetNextActiveTask()` 返回下一个应该显示的任务
   - 定时器触发切换

2. `DynamicIsland.cpp` 在 `UpdatePhysics()` 中调用轮播逻辑

---

### 任务 1.5: 内存泄漏修复

**优先级: P0 (阻塞)**
**依赖: 无**
**预估工作量: 2 小时**

**问题描述:**
多处使用 `new`/`delete` 但存在泄漏风险。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `DynamicIsland.cpp` | 修复 `WM_UPDATE_ALBUM_ART_MEMORY` 和 `WM_SHOW_ALERT_MEMORY` 中的内存管理 |
| `Messages.h` | 使用智能指针或确保所有分配点都有对应释放 |

**具体修改:**

1. `Messages.h` 的 `ImageData` 结构体考虑使用 `std::unique_ptr`
2. `DynamicIsland.cpp` 第 742-751 行检查 `imgData->data` 所有权

---

## Phase 2: UI/UX 优化

### 目标: 提升视觉体验和交互流畅度

---

### 任务 2.1: MusicPlayerComponent 集成

**优先级: P1**
**依赖: Phase 1 完成**
**预估工作量: 6 小时**

**问题描述:**
`MusicPlayerComponent` 有独立渲染类但未被使用，音乐播放器渲染逻辑散落在 `RenderEngine` 中。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `RenderEngine.cpp` | 将音乐播放器相关渲染提取到 `MusicPlayerComponent::RenderExpanded()` |
| `RenderEngine.h` | 移除音乐播放器相关成员变量 |
| `DynamicIsland.cpp` | 创建 `MusicPlayerComponent` 实例并调用 |

**具体修改:**

1. `RenderEngine.cpp` 中 `DrawCapsule()` 的音乐播放器渲染部分移至 `MusicPlayerComponent`
2. `DynamicIsland.cpp` 创建 `m_musicPlayer`
3. `MusicPlayerComponent::RenderExpanded()` 接收所有音乐相关参数进行渲染

---

### 任务 2.2: FunctionMenu 功能菜单完善

**优先级: P1**
**依赖: Phase 1 完成**
**预估工作量: 8 小时**

**问题描述:**
`FunctionMenu.cpp` 大部分功能为占位实现。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `FunctionMenu.cpp` | 实现时钟、天气、日历、截图、计算器功能 |
| `FunctionMenu.h` | 添加功能入口结构体 |

**具体修改:**

| 功能 | 实现方式 |
|-----|---------|
| 时钟 | 显示当前时间，每秒更新 |
| 天气 | 调用天气 API (如 OpenWeatherMap) |
| 日历 | 显示当月日历，标记当前日期 |
| 截图 | 调用 `PrintScreen` 或 `snippingtool` |
| 计算器 | 简单表达式求值 |
| 系统监控 | 显示 CPU/内存使用率 |

**依赖文件:** `FunctionMenu.cpp`, `FunctionMenu.h`

---

### 任务 2.3: 多显示器 DPI 支持增强

**优先级: P2**
**依赖: 无**
**预估工作量: 3 小时**

**问题描述:**
当窗口移动到不同 DPI 的显示器时，渲染可能失真。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `DynamicIsland.cpp` | 在 `WM_MOVE` 时重新获取窗口 DPI |
| `RenderEngine.cpp` | `Resize()` 时重新计算缩放 |

**具体修改:**

`DynamicIsland.cpp` 的 `HandleMessage` 添加:
```cpp
case WM_MOVE: {
    m_currentDpi = GetDpiForWindow(hwnd);
    m_dpiScale = m_currentDpi / 96.0f;
    m_renderer.SetDpi((float)m_currentDpi);
    break;
}
```

---

### 任务 2.4: 动画流畅度优化

**优先级: P2**
**依赖: 无**
**预估工作量: 2 小时**

**问题描述:**
弹簧动画参数可能不够跟手。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `Constants.h` | `Animation::TENSION` 和 `Animation::FRICTION` 可调整为动态值 |
| `DynamicIsland.cpp` | `UpdatePhysics()` 中动画参数根据状态动态调整 |

---

## Phase 3: 扩展功能

### 目标: 完成占位功能，实现插件系统和网络传输

---

### 任务 3.1: PluginManager 插件系统实现

**优先级: P2**
**依赖: Phase 2 任务 2.2 (FunctionMenu)**
**预估工作量: 6 小时**

**问题描述:**
`PluginManager.h` 仅定义了接口，无实际加载逻辑。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `PluginManager.cpp` (新建) | 实现 `LoadPlugins()`、动态库加载 (`LoadLibrary`) |
| `DynamicIsland.cpp` | 初始化时调用 `m_pluginManager.LoadPlugins()` |

**具体修改:**

1. 新建 `PluginManager.cpp`:
```cpp
bool PluginManager::LoadPlugins(const std::wstring& pluginDir) {
    // 搜索 pluginDir 下的 .dll 文件
    // 使用 LoadLibrary 加载
    // 调用 IPlugin::Initialize()
}
```

2. `DynamicIsland::Initialize()` 中添加:
```cpp
m_pluginManager.LoadPlugins(pluginDir);
```

---

### 任务 3.2: NetworkDiscovery 局域网设备发现

**优先级: P2**
**依赖: 任务 3.1 (PluginManager)**
**预估工作量: 5 小时**

**问题描述:**
`NetworkDiscovery.h` 存在但无 `.cpp` 实现。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `NetworkDiscovery.cpp` (新建) | 实现 UDP 广播监听、设备列表维护 |
| `LanTransferManager.cpp` (新建) | 实现 TCP 文件传输 |

**具体修改:**

1. `NetworkDiscovery.cpp`:
   - 创建 UDP socket 绑定 `BROADCAST_PORT`
   - 接收设备公告报文
   - 解析设备名/IP/端口

2. `LanTransferManager.cpp`:
   - 实现 `SendFile()` 使用 TCP socket 传输
   - 实现进度回调

---

### 任务 3.3: Pomodoro UI 集成

**优先级: P2**
**依赖: 任务 1.2 (PomodoroTimer)**
**预估工作量: 3 小时**

**问题描述:**
番茄钟实现后需要 UI 显示。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `RenderEngine.cpp` | 添加番茄钟渲染方法 `DrawPomodoro()` |
| `DynamicIsland.cpp` | 番茄钟激活时展开特殊 UI |

**具体修改:**
当 Pomodoro 激活时，`m_state` 切换为 `Pomodoro`，岛屿显示倒计时和开始/暂停按钮。

---

### 任务 3.4: 设置持久化增强

**优先级: P1**
**依赖: 无**
**预估工作量: 2 小时**

**问题描述:**
当前使用 `config.ini`，可改为 JSON 格式便于扩展。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `SettingsWindow.cpp` | 改用 JSON 读写配置 |
| `DynamicIsland.cpp` | `LoadConfig()` 改为读取 JSON |

**依赖文件:** `SettingsWindow.cpp`, `DynamicIsland.cpp`

---

## Phase 4: 稳定性与测试

### 目标: 提升稳定性、修复边界情况

---

### 任务 4.1: 异常处理增强

**优先级: P1**
**依赖: Phase 1 完成**
**预估工作量: 3 小时**

**问题描述:**
多处缺少空指针检查，崩溃风险高。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `RenderEngine.cpp` | 所有 COM 接口调用前检查 nullptr |
| `MediaMonitor.cpp` | 媒体会话回调中增加异常捕获 |
| `LyricsMonitor.cpp` | HTTP 请求失败处理 |

---

### 任务 4.2: 边界情况处理

**优先级: P1**
**依赖: 任务 4.1**
**预估工作量: 2 小时**

**问题描述:**
窗口拖拽到屏幕边缘、多显示器等边界情况。

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| `DynamicIsland.cpp` | `WM_MOVING` 时限制窗口在屏幕工作区内 |
| `RenderEngine.cpp` | 专辑封面/歌词为空时的占位图 |

---

### 任务 4.3: 编译警告清理

**优先级: P2**
**依赖: 无**
**预估工作量: 1 小时**

**修改文件:**

| 文件 | 修改内容 |
|-----|---------|
| 所有 .cpp/.h 文件 | 修复 MSVC 编译警告 (C4244, C4800 等) |

---

## 任务依赖关系图

```
Phase 1: 核心功能完善
├── 1.1 常量统一重构
├── 1.2 PomodoroTimer 实现  ──────────┐
├── 1.3 TaskDetector 实现  ──────┐    │
├── 1.4 IslandCarousel 实现  ───┴─┐    │
│                                  │    │
└──────────────────────────────────┼────┘
                                   │
Phase 2: UI/UX 优化                │
├── 2.1 MusicPlayerComponent 集成  │
├── 2.2 FunctionMenu 完善  ────────┤
├── 2.3 多显示器 DPI 支持  ◄───────┤
└── 2.4 动画流畅度优化  ◄───────────┘

Phase 3: 扩展功能
├── 3.1 PluginManager 实现  ───────┐
├── 3.2 NetworkDiscovery 实现  ────┤
├── 3.3 Pomodoro UI 集成  ─────────┤
└── 3.4 设置持久化增强  ◄───────────┘

Phase 4: 稳定性与测试
├── 4.1 异常处理增强
├── 4.2 边界情况处理
└── 4.3 编译警告清理
```

---

## 优先级总结

### P0 (阻塞性 - 优先处理)
| 任务 ID | 任务名称 |
|---------|---------|
| 1.1 | 常量统一重构 |
| 1.5 | 内存泄漏修复 |

### P1 (重要 - Phase 1/2 优先)
| 任务 ID | 任务名称 |
|---------|---------|
| 1.2 | PomodoroTimer 实现 |
| 1.3 | TaskDetector 实现 |
| 1.4 | IslandCarousel 实现 |
| 2.1 | MusicPlayerComponent 集成 |
| 2.2 | FunctionMenu 功能完善 |
| 3.4 | 设置持久化增强 |
| 4.1 | 异常处理增强 |
| 4.2 | 边界情况处理 |

### P2 (优化 - Phase 3/4)
| 任务 ID | 任务名称 |
|---------|---------|
| 2.3 | 多显示器 DPI 支持 |
| 2.4 | 动画流畅度优化 |
| 3.1 | PluginManager 插件系统 |
| 3.2 | NetworkDiscovery 局域网发现 |
| 3.3 | Pomodoro UI 集成 |
| 4.3 | 编译警告清理 |

---

## 里程碑

| 里程碑 | 包含任务 | 预期产出 |
|-------|---------|---------|
| M1: 稳定基础 | 1.1, 1.5, 4.1, 4.2 | 可测试的稳定版本 |
| M2: 核心完整 | 1.2, 1.3, 1.4, 2.1 | 番茄钟、任务检测、轮播 |
| M3: 功能丰富 | 2.2, 3.4 | 完整功能菜单、设置持久化 |
| M4: 生态扩展 | 3.1, 3.2, 3.3 | 插件系统、局域网传输 |
| M5: 生产就绪 | 2.3, 2.4, 4.3 | 流畅的多显示器体验 |

---

*最后更新: 2026-03-25*
