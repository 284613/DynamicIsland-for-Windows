# 多岛轮播系统 - 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现任务轮播式多岛系统，支持点击切换不同任务显示，无任务时显示自定义功能菜单

**Architecture:** 基于现有的 IslandManager 框架扩展，新增 IslandCarousel 轮播管理器和 TaskDetector 统一任务检测，RenderEngine 支持圆圈渲染，功能菜单独立组件

**Tech Stack:** C++/Win32, Direct2D RenderEngine, JSON 配置

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `IslandCarousel.h/cpp` | 新建 - 轮播管理器，核心逻辑 |
| `TaskDetector.h/cpp` | 新建 - 统一任务检测和状态管理 |
| `FunctionMenu.h/cpp` | 新建 - 功能菜单组件 |
| `RenderEngine.h/cpp` | 修改 - 添加圆圈渲染方法 |
| `DynamicIsland.h/cpp` | 修改 - 集成轮播系统 |
| `IslandManager.h/cpp` | 修改 - 适配多岛模式 |
| `SettingsWindow.h/cpp` | 修改 - 功能菜单配置界面 |
| `Constants.h` | 修改 - 添加新消息类型 |

---

## Phase 1: 基础框架

### Task 1: 创建 TaskDetector 任务检测器

**Files:**
- Create: `TaskDetector.h`
- Create: `TaskDetector.cpp`

- [ ] **Step 1: 创建 TaskDetector.h**

```cpp
#pragma once
#include <windows.h>
#include <vector>
#include <mutex>

enum class TaskType {
    None,
    Music,      // 音乐播放
    Pomodoro,   // 番茄钟
    Notification, // 通知
    Transfer,   // 文件传输
    Lyrics      // 歌词
};

struct TaskInfo {
    TaskType type;
    std::wstring title;     // 显示标题
    std::wstring subtitle;  // 副标题
    bool isActive;          // 是否激活
};

class TaskDetector {
public:
    static TaskDetector& Instance();

    // 获取当前活动任务列表
    std::vector<TaskInfo> GetActiveTasks();

    // 任务数量
    int GetTaskCount();

    // 更新回调
    void SetUpdateCallback(std::function<void()> callback);

private:
    TaskDetector();
    ~TaskDetector();

    void CheckMusicTask();
    void CheckPomodoroTask();
    void CheckNotificationTask();

    std::vector<TaskInfo> m_tasks;
    std::mutex m_mutex;
    std::function<void()> m_updateCallback;
};
```

- [ ] **Step 2: 创建 TaskDetector.cpp - 实现单例和任务检测**

```cpp
#include "TaskDetector.h"
#include "MediaMonitor.h"
#include "PomodoroTimer.h"
#include "NotificationMonitor.h"

TaskDetector& TaskDetector::Instance() {
    static TaskDetector instance;
    return instance;
}

std::vector<TaskInfo> TaskDetector::GetActiveTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<TaskInfo> active;
    for (const auto& task : m_tasks) {
        if (task.isActive) {
            active.push_back(task);
        }
    }
    return active;
}

int TaskDetector::GetTaskCount() {
    return (int)GetActiveTasks().size();
}
```

- [ ] **Step 3: 提交**

```bash
git add TaskDetector.h TaskDetector.cpp
git commit -m "feat: add TaskDetector for unified task detection"
```

---

### Task 2: 创建 IslandCarousel 轮播管理器

**Files:**
- Create: `IslandCarousel.h`
- Create: `IslandCarousel.cpp`

- [ ] **Step 1: 创建 IslandCarousel.h**

```cpp
#pragma once
#include "TaskDetector.h"
#include <windows.h>
#include <functional>

enum class CarouselState {
    Mini,        // 仅显示时间
    SingleTask,  // 单任务直接显示
    MultiTask    // 多任务轮播
};

class IslandCarousel {
public:
    IslandCarousel();

    // 获取当前状态
    CarouselState GetState() const { return m_state; }

    // 获取当前任务索引（多任务模式）
    int GetCurrentIndex() const { return m_currentIndex; }

    // 总任务数
    int GetTaskCount() const { return m_taskCount; }

    // 切换到下一个任务
    void NextTask();

    // 切换到上一个任务
    void PrevTask();

    // 切换到指定任务
    void GoToTask(int index);

    // 更新状态（被 TaskDetector 调用）
    void Update();

    // 设置切换回调
    void SetSwitchCallback(std::function<void()> callback);

private:
    void UpdateState();

    CarouselState m_state;
    int m_currentIndex;
    int m_taskCount;
    std::function<void()> m_switchCallback;
};
```

- [ ] **Step 2: 创建 IslandCarousel.cpp**

```cpp
#include "IslandCarousel.h"

IslandCarousel::IslandCarousel()
    : m_state(CarouselState::Mini)
    , m_currentIndex(0)
    , m_taskCount(0)
{
}

void IslandCarousel::Update() {
    m_taskCount = TaskDetector::Instance().GetTaskCount();
    UpdateState();
}

void IslandCarousel::UpdateState() {
    if (m_taskCount == 0) {
        m_state = CarouselState::Mini;
    } else if (m_taskCount == 1) {
        m_state = CarouselState::SingleTask;
    } else {
        m_state = CarouselState::MultiTask;
    }
}

void IslandCarousel::NextTask() {
    if (m_taskCount > 0) {
        m_currentIndex = (m_currentIndex + 1) % m_taskCount;
        if (m_switchCallback) m_switchCallback();
    }
}

void IslandCarousel::PrevTask() {
    if (m_taskCount > 0) {
        m_currentIndex = (m_currentIndex - 1 + m_taskCount) % m_taskCount;
        if (m_switchCallback) m_switchCallback();
    }
}

void IslandCarousel::GoToTask(int index) {
    if (index >= 0 && index < m_taskCount) {
        m_currentIndex = index;
        if (m_switchCallback) m_switchCallback();
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add IslandCarousel.h IslandCarousel.cpp
git commit -m "feat: add IslandCarousel for task navigation"
```

---

## Phase 2: 渲染适配

### Task 3: 扩展 RenderEngine 支持圆圈渲染

**Files:**
- Modify: `RenderEngine.h`
- Modify: `RenderEngine.cpp`

- [ ] **Step 1: 在 RenderEngine.h 添加圆圈渲染方法**

在 `RenderEngine` 类中添加：

```cpp
// 渲染任务圆圈
void RenderTaskCircle(float x, float y, float radius, TaskType type, bool isActive);
```

- [ ] **Step 2: 在 RenderEngine.cpp 实现圆圈渲染**

```cpp
void RenderEngine::RenderTaskCircle(float x, float y, float radius, TaskType type, bool isActive) {
    auto brush = m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(
        isActive ? D2D1::ColorF::LightBlue : D2D1::ColorF::Gray));

    m_renderTarget->FillEllipse(D2D1::Ellipse(
        D2D1::Point2F(x, y), radius, radius), brush);

    // 根据 type 绘制不同图标
    switch (type) {
        case TaskType::Music:
            DrawMusicIcon(x, y, radius * 0.5f);
            break;
        case TaskType::Pomodoro:
            DrawTimerIcon(x, y, radius * 0.5f);
            break;
        // ...
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add RenderEngine.h RenderEngine.cpp
git commit -m "feat: add task circle rendering to RenderEngine"
```

---

### Task 4: 添加圆圈点击区域检测

**Files:**
- Modify: `DynamicIsland.h` - 添加 `HitTestCircle`
- Modify: `DynamicIsland.cpp` - 实现点击逻辑

- [ ] **Step 1: 在 DynamicIsland.h 添加点击检测方法**

```cpp
// 圆圈点击区域检测
enum class CircleArea {
    None,
    Left,   // 左圆圈
    Center,  // 主岛
    Right   // 右圆圈
};
CircleArea HitTestCircles(POINT pt);
```

- [ ] **Step 2: 在 DynamicIsland.cpp 实现点击处理**

```cpp
DynamicIsland::CircleArea DynamicIsland::HitTestCircles(POINT pt) {
    // 计算左圆圈区域
    float leftCircleX = m_currentWidth / 2.0f - CIRCLE_SPACING;
    // 计算右圆圈区域
    float rightCircleX = m_currentWidth / 2.0f + CIRCLE_SPACING;

    if (PtInCircle(pt, leftCircleX, y, CIRCLE_RADIUS))
        return CircleArea::Left;
    if (PtInCircle(pt, rightCircleX, y, CIRCLE_RADIUS))
        return CircleArea::Right;
    if (PtInRect(GetMainIslandRect(), pt))
        return CircleArea::Center;
    return CircleArea::None;
}
```

- [ ] **Step 3: 在消息处理中添加点击响应**

在 `HandleMessage` 的 `WM_LBUTTONDOWN` 处理中添加：

```cpp
case WM_LBUTTONDOWN: {
    POINT pt = { LOWORD(lParam), HIWORD(lParam) };
    auto area = HitTestCircles(pt);
    if (area == CircleArea::Left) {
        m_carousel.PrevTask();
    } else if (area == CircleArea::Right || area == CircleArea::Center) {
        m_carousel.NextTask();
    }
    break;
}
```

- [ ] **Step 4: 提交**

```bash
git add DynamicIsland.h DynamicIsland.cpp
git commit -m "feat: add circle hit-test and click handling"
```

---

## Phase 3: 功能菜单

### Task 5: 创建 FunctionMenu 功能菜单组件

**Files:**
- Create: `FunctionMenu.h`
- Create: `FunctionMenu.cpp`

- [ ] **Step 1: 创建 FunctionMenu.h**

```cpp
#pragma once
#include <windows.h>
#include <vector>
#include <string>

enum class FunctionType {
    Clock,
    Weather,
    Calendar,
    Screenshot,
    Calculator,
    Monitor
};

struct FunctionItem {
    FunctionType type;
    std::wstring name;
    std::wstring icon;
    bool enabled;
};

class FunctionMenu {
public:
    FunctionMenu();

    void SetFunctions(const std::vector<FunctionItem>& funcs);
    std::vector<FunctionItem> GetEnabledFunctions();

    void Render();
    int HitTest(POINT pt);  // 返回点击的索引，-1 表示未点击

    void Show() { m_visible = true; }
    void Hide() { m_visible = false; }
    bool IsVisible() const { return m_visible; }

private:
    std::vector<FunctionItem> m_functions;
    bool m_visible;
    RECT m_bounds;
};
```

- [ ] **Step 2: 创建 FunctionMenu.cpp**

```cpp
#include "FunctionMenu.h"
#include "RenderEngine.h"

FunctionMenu::FunctionMenu() : m_visible(false) {}

void FunctionMenu::Render() {
    if (!m_visible) return;

    float startX = 10.0f;
    float startY = MAIN_ISLAND_HEIGHT + 10.0f;
    float itemSize = 40.0f;
    float spacing = 10.0f;

    for (size_t i = 0; i < m_functions.size(); ++i) {
        const auto& func = m_functions[i];
        float x = startX + i * (itemSize + spacing);

        // 渲染功能图标背景
        RenderEngine::Instance().FillRoundedRect(
            x, startY, itemSize, itemSize, 8.0f,
            func.enabled ? COLOR_ACCENT : COLOR_GRAY);

        // 渲染功能图标/文字
        // ...
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add FunctionMenu.h FunctionMenu.cpp
git commit -m "feat: add FunctionMenu component"
```

---

### Task 6: 创建功能菜单配置界面

**Files:**
- Modify: `SettingsWindow.h` - 添加功能菜单配置页
- Modify: `SettingsWindow.cpp` - 实现配置 UI

- [ ] **Step 1: 在 SettingsWindow.h 添加配置方法**

```cpp
// 功能菜单配置
void CreateFunctionMenuSettings();
void SaveFunctionMenuSettings();
```

- [ ] **Step 2: 在 SettingsWindow.cpp 实现复选框列表**

```cpp
void SettingsWindow::CreateFunctionMenuSettings() {
    // 添加功能菜单配置组
    AddCheckboxGroup(L"功能菜单", {
        { L"时钟", L"function_clock" },
        { L"天气", L"function_weather" },
        { L"日历", L"function_calendar" },
        { L"截图", L"function_screenshot" },
        { L"计算器", L"function_calculator" },
        { L"系统监控", L"function_monitor" }
    });
}
```

- [ ] **Step 3: 提交**

```bash
git add SettingsWindow.h SettingsWindow.cpp
git commit -m "feat: add function menu settings page"
```

---

## Phase 4: 整合测试

### Task 7: 集成 TaskDetector 到 DynamicIsland

**Files:**
- Modify: `DynamicIsland.h` - 添加 TaskDetector 和 FunctionMenu 成员
- Modify: `DynamicIsland.cpp` - 实现轮播渲染逻辑

- [ ] **Step 1: 在 DynamicIsland.h 添加成员**

```cpp
#include "IslandCarousel.h"
#include "TaskDetector.h"
#include "FunctionMenu.h"

private:
    IslandCarousel m_carousel;
    FunctionMenu m_functionMenu;
```

- [ ] **Step 2: 在 DynamicIsland.cpp 实现渲染分支**

在渲染逻辑中添加：

```cpp
void DynamicIsland::RenderContent() {
    switch (m_carousel.GetState()) {
        case CarouselState::Mini:
            RenderMiniIsland();
            if (m_carousel.GetTaskCount() == 0) {
                m_functionMenu.Render();
            }
            break;

        case CarouselState::SingleTask:
            RenderSingleTaskIsland();
            break;

        case CarouselState::MultiTask:
            RenderMultiTaskCarousel();
            break;
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add DynamicIsland.h DynamicIsland.cpp
git commit -m "feat: integrate carousel and function menu"
```

---

### Task 8: 测试和配置持久化

**Files:**
- Modify: `Constants.h` - 添加配置键
- Modify: `SettingsWindow.cpp` - 保存/加载功能配置

- [ ] **Step 1: 添加配置键到 Constants.h**

```cpp
// 功能菜单配置键
#define CONFIG_FUNCTION_CLOCK       L"function_clock"
#define CONFIG_FUNCTION_WEATHER      L"function_weather"
#define CONFIG_FUNCTION_CALENDAR     L"function_calendar"
#define CONFIG_FUNCTION_SCREENSHOT   L"function_screenshot"
#define CONFIG_FUNCTION_CALCULATOR   L"function_calculator"
#define CONFIG_FUNCTION_MONITOR      L"function_monitor"
```

- [ ] **Step 2: 在 SettingsWindow 中实现持久化**

```cpp
void SettingsWindow::SaveFunctionMenuSettings() {
    for (int i = 0; i < (int)FunctionType::Count; ++i) {
        SaveBool(GetConfigKey(i), IsFunctionEnabled(i));
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add Constants.h SettingsWindow.cpp
git commit -m "feat: add function menu configuration persistence"
```

---

## 任务完成检查清单

- [ ] Task 1: TaskDetector 创建完成
- [ ] Task 2: IslandCarousel 创建完成
- [ ] Task 3: RenderEngine 圆圈渲染完成
- [ ] Task 4: 圆圈点击检测完成
- [ ] Task 5: FunctionMenu 组件完成
- [ ] Task 6: 配置界面完成
- [ ] Task 7: 集成到 DynamicIsland 完成
- [ ] Task 8: 配置持久化完成

---

**Plan saved to:** `docs/superpowers/plans/2026-03-19-multi-island-carousel.md`
