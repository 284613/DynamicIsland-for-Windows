# 计划：紧凑态任务切换（鼠标滚轮）

## 问题

当前优先级表 `m_priorityTable` 是固定扫描：`MusicCompact`(50) 永远压过 `PomodoroCompact`(45)，`PomodoroCompact` 永远压过 `Idle`(10)。这意味着：

- 在听音乐时，紧凑态只能看到音乐滚动标题，无法切换到时钟/番茄钟入口
- 在番茄计时时，紧凑态只能看到倒计时，无法切换回音乐或时钟
- 用户必须先点击展开，再关闭，才能接触到被优先级压住的功能

## 目标

在紧凑态（`Collapsed`，岛高度 ≤ 40px 区域）下，**滚动鼠标中键** 可以在当前所有"活跃任务"之间循环切换显示内容。

## 术语

| 术语 | 含义 |
|------|------|
| 活跃任务 | 当前有内容可显示的紧凑态模式（`MusicCompact` / `PomodoroCompact` / `Idle`） |
| 任务列表 | 运行时动态收集的活跃任务有序列表 |
| 用户锁定 | 用户通过滚轮手动选定某个任务后，优先级表暂时被覆盖 |

## 设计

### 核心思路

引入一个 **用户锁定覆盖层**，不修改已有的优先级表逻辑，而是在 `DetermineDisplayMode()` 的返回之前插入一层覆盖判断。

### 数据结构

在 `DynamicIsland` 类中新增：

```cpp
// ---------- 紧凑态任务切换 ----------
bool                              m_compactTaskLocked = false;   // 用户是否手动锁定了某个紧凑任务
IslandDisplayMode                 m_lockedCompactMode = IslandDisplayMode::Idle; // 锁定的目标模式
std::vector<IslandDisplayMode>    m_activeCompactTasks;          // 当前活跃的紧凑任务列表（动态刷新）
```

### 活跃任务收集

新增方法 `CollectActiveCompactTasks()`，每次滚轮事件触发时调用：

```cpp
void DynamicIsland::CollectActiveCompactTasks() {
    m_activeCompactTasks.clear();

    // 按固定顺序收集（与优先级表中紧凑态项的排列一致）
    if (m_mediaMonitor.IsPlaying()) {
        m_activeCompactTasks.push_back(IslandDisplayMode::MusicCompact);
    }
    auto pomodoro = m_renderer.GetComponent<PomodoroComponent>();
    if (pomodoro && pomodoro->HasActiveSession()) {
        m_activeCompactTasks.push_back(IslandDisplayMode::PomodoroCompact);
    }
    // Idle（时钟）始终可用
    m_activeCompactTasks.push_back(IslandDisplayMode::Idle);
}
```

### 滚轮切换逻辑

修改 `WM_MOUSEWHEEL` 处理：当 `m_state == IslandState::Collapsed` 且岛高度处于紧凑区间时，滚轮不再调音量，而是切换任务。

```
伪代码：
1. 收集活跃任务列表
2. 如果列表只有 1 项 → 不切换，直接返回
3. 找到当前显示模式在列表中的位置
4. delta > 0（向上滚）→ 上一个任务；delta < 0（向下滚）→ 下一个任务
5. 设置 m_compactTaskLocked = true，m_lockedCompactMode = 新任务
6. 调用 TransitionTo(新任务) 触发弹簧动画
7. 启动/重置一个锁定超时定时器（可选，见下方讨论）
```

### DetermineDisplayMode 覆盖

在 `DetermineDisplayMode()` 中，在优先级表扫描之后、返回之前插入覆盖逻辑：

```cpp
IslandDisplayMode DynamicIsland::DetermineDisplayMode() const {
    // 已有的优先级表扫描
    IslandDisplayMode result = IslandDisplayMode::Idle;
    for (const auto& entry : m_priorityTable) {
        if (entry.condition()) {
            result = entry.mode;
            break;
        }
    }

    // ---- 新增：紧凑态用户锁定覆盖 ----
    if (m_compactTaskLocked && m_state == IslandState::Collapsed) {
        // 只在锁定目标仍然活跃时覆盖
        bool stillValid = false;
        switch (m_lockedCompactMode) {
            case IslandDisplayMode::MusicCompact:
                stillValid = m_mediaMonitor.IsPlaying();
                break;
            case IslandDisplayMode::PomodoroCompact:
                stillValid = (pomodoro && pomodoro->HasActiveSession());
                break;
            case IslandDisplayMode::Idle:
                stillValid = true;  // 时钟始终可用
                break;
            default: break;
        }
        if (stillValid) {
            result = m_lockedCompactMode;
        } else {
            // 锁定目标已失效，解除锁定
            const_cast<DynamicIsland*>(this)->m_compactTaskLocked = false;
        }
    }

    return result;
}
```

> 注意：Alert / Volume / WeatherExpanded 等高优先级模式不受覆盖影响 —— 它们的条件先被扫描到时，`result` 已经是高优先级模式，而覆盖层只在 `Collapsed` 状态下生效，这些展开态不会处于 `Collapsed`。

### 锁定解除时机

以下场景自动解除锁定（`m_compactTaskLocked = false`）：

| 场景 | 位置 |
|------|------|
| 用户点击展开主岛 | `WM_LBUTTONUP` 展开分支 |
| Alert 弹窗到来 | `ShowAlert()` |
| 锁定目标任务失效 | `DetermineDisplayMode()` 内部检查 |
| 岛回到 Mini 态 | 自动收缩定时器触发 `SetTargetSize(96, 24)` 时 |

**不需要** 超时自动解除 —— 用户手动选的任务应该一直保持，直到上述事件发生。

### 滚轮与音量调节的冲突处理

当前滚轮在 `MusicCompact` / `MusicExpanded` 下用于调节音量。改动后：

| 状态 | 滚轮行为 |
|------|----------|
| **Collapsed + 紧凑态**（高度 ≤ 40） | **切换任务**（新行为） |
| **MusicExpanded**（展开态） | 调节音量（保持不变） |
| **WeatherExpanded** | 切换小时/逐日视图（保持不变） |
| **其他展开态** | 无操作（保持不变） |

关键变化：紧凑态下的音乐滚轮调音量被替换为任务切换。音量调节仍可在音乐展开态中使用。

## 涉及文件

| 文件 | 改动 |
|------|------|
| `include/DynamicIsland.h` | 新增 3 个成员变量、`CollectActiveCompactTasks()` 声明 |
| `src/DynamicIsland.cpp` | 修改 `WM_MOUSEWHEEL`、修改 `DetermineDisplayMode()`、多处添加锁定解除 |

不需要改动 `LayoutController`、`RenderEngine` 或任何 `Component` —— 切换只是改变 `DisplayMode`，渲染层已经知道怎么画每种模式。

## 交互流程示例

### 场景 A：音乐 + 番茄 + 时钟

```
用户正在听音乐，同时有番茄计时
紧凑态显示：[♪ 歌曲标题滚动]     ← MusicCompact（优先级赢了）

用户滚动 ↓：
  活跃任务列表 = [MusicCompact, PomodoroCompact, Idle]
  当前 = MusicCompact (index 0)
  切换到 → PomodoroCompact (index 1)
  紧凑态显示：[🔴 12:34  ⏸ ⏹]

用户再滚动 ↓：
  切换到 → Idle (index 2)
  紧凑态显示：[14:30  ☀]            ← 时钟 + 天气图标

用户再滚动 ↓：
  切换到 → MusicCompact (index 0)   ← 循环回来
  紧凑态显示：[♪ 歌曲标题滚动]

用户点击主岛 → 展开当前锁定的模式 → 锁定解除
```

### 场景 B：仅有音乐

```
活跃任务列表 = [MusicCompact, Idle]

滚动 ↓ → 切换到 Idle（时钟）
滚动 ↓ → 切换回 MusicCompact
```

### 场景 C：仅 Idle

```
活跃任务列表 = [Idle]

滚动 → 只有 1 个任务，不切换
```

## 实施步骤

### 第 1 步：新增成员变量

在 `include/DynamicIsland.h` 中添加 3 个字段和 1 个方法声明。

### 第 2 步：实现 CollectActiveCompactTasks()

在 `src/DynamicIsland.cpp` 中实现活跃任务收集逻辑。

### 第 3 步：修改 WM_MOUSEWHEEL

在 `WM_MOUSEWHEEL` case 最前方加入紧凑态判断分支：
- `m_state == Collapsed` 且当前显示模式为 `MusicCompact` / `PomodoroCompact` / `Idle` 时走任务切换
- 其余情况走原有逻辑（音量 / 天气视图切换）

### 第 4 步：修改 DetermineDisplayMode()

在优先级表扫描后插入锁定覆盖逻辑。

### 第 5 步：添加锁定解除

在 `WM_LBUTTONUP` 展开分支、`ShowAlert()`、Mini 态收缩定时器处清除锁定。

### 第 6 步：编译测试

```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
```

测试矩阵：
- [ ] 仅 Idle：滚轮无反应
- [ ] 音乐播放中：滚轮可切换到 Idle，再切回 MusicCompact
- [ ] 音乐 + 番茄：三个任务循环切换
- [ ] 切换到 Idle 后点击 → 展开对应面板，锁定解除
- [ ] 切换到 PomodoroCompact 后点击 → 展开番茄面板
- [ ] Alert 弹窗到来 → 锁定自动解除
- [ ] 音乐停止 → 如果当前锁定在 MusicCompact，自动回退到下一个可用任务
- [ ] 展开态滚轮 → 音量/天气切换行为不受影响
