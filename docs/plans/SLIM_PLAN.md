# DynamicIsland.cpp 瘦身计划

> 目标：将 `src/DynamicIsland.cpp` 从 **4211 行** 压缩到 **~2200 行**
> 原则：不改变任何运行时行为，只做结构拆分和重复消除

---

## Phase 0 — 清理空行（-1200 行）

**范围**：全文件

当前文件中几乎每条语句后都有 4-5 个连续空行，这是历史编辑遗留。统一规范为：

- 函数之间：**1 个空行**
- 函数内逻辑块之间：**1 个空行**
- 连续空行上限：**2 个**

**风险**：无。纯格式变更，不影响编译和运行。

**预估**：4211 → ~3000 行

---

## Phase 1 — 提取公共工具方法（-90 行）

### 1.1 `POINT LogicalFromPhysical(POINT physicalPt)`

**问题**：以下代码在 `WM_NCHITTEST`、`WM_MOUSEMOVE`、`WM_LBUTTONDOWN`、`WM_LBUTTONUP` 中重复了 4 次：

```cpp
pt.x = (LONG)std::round(physicalPt.x / m_dpiScale);
pt.y = (LONG)std::round(physicalPt.y / m_dpiScale);
```

**做法**：在 `DynamicIsland` 类中添加内联方法：

```cpp
POINT LogicalFromPhysical(LPARAM lParam) const {
    POINT phys = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    return { (LONG)std::round(phys.x / m_dpiScale),
             (LONG)std::round(phys.y / m_dpiScale) };
}
```

然后将 4 处替换为单行调用。

### 1.2 `std::wstring GetConfigPath()`

**问题**：INI 路径构造出现了 3 处（匿名命名空间 `GetInputDebugLogPath`、`LoadConfig`、`WM_SETTINGS_APPLY` lambda），各自用不同方式拼路径。

**做法**：在匿名命名空间中添加：

```cpp
std::wstring GetExeDirectory() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath);
}

std::wstring GetConfigPath() {
    return GetExeDirectory() + L"\\config.ini";
}
```

然后将 `LoadConfig()`、`WM_SETTINGS_APPLY`、`GetInputDebugLogPath()` 统一调用。

### 1.3 `ProgressBarLayout GetProgressBarLayout()`

**问题**：进度条坐标计算（`artSize=60, artLeft=left+20, textLeft=artLeft+artSize+15, progressBarLeft, progressBarRight`）在 `WM_MOUSEMOVE` 和 `WM_LBUTTONDOWN` 中完全重复。

**做法**：添加私有方法返回结构体：

```cpp
struct ProgressBarLayout { float left; float right; };

ProgressBarLayout DynamicIsland::GetProgressBarLayout() const {
    float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
    float right = left + GetCurrentWidth();
    float artLeft = left + 20.0f;
    float textLeft = artLeft + 60.0f + 15.0f;
    float barLeft = textLeft - 80.0f;
    float barRight = textLeft + (right - 20.0f - textLeft);
    return { barLeft, barRight };
}
```

将 `WM_MOUSEMOVE`（第 1858-1906 行）和 `WM_LBUTTONDOWN`（第 2443-2491 行）中的重复代码替换为调用。同时提取 `ClampProgress` 内联计算。

---

## Phase 2 — 提取文件副岛交互子系统（-250 行）

**范围**：第 293-496 行的全部文件副岛输入处理

### 新文件

- `src/FileSecondaryInput.cpp`
- `include/FileSecondaryInput.h`

### 要移动的方法

| 方法 | 当前行号 | 说明 |
|------|---------|------|
| `HandleFileSecondaryMouseDown` | 324-390 | 副岛鼠标按下 |
| `HandleFileSecondaryMouseMove` | 392-445 | 副岛鼠标移动+拖拽 |
| `HandleFileSecondaryMouseUp` | 447-496 | 副岛鼠标释放+点击/双击 |
| `ResetFileSecondaryInteraction` | 293-300 | 重置交互状态 |
| `RemoveFileStashIndex` | 312-322 | 按索引移除文件 |
| `ShowFileStashLimitAlert` | 302-310 | 暂存已满提示 |

### 实现方式

**方案 A（推荐）：保持为 DynamicIsland 的成员方法，只做文件拆分**

将上述方法的实现移到 `src/FileSecondaryInput.cpp`，但它们仍然是 `DynamicIsland::` 的成员。头文件不需要改动，只是把 `.cpp` 实现分拆。这样不需要新建类，也不需要传递大量指针。

`src/FileSecondaryInput.cpp` 顶部：

```cpp
#include "DynamicIsland.h"
#include "EventBus.h"
// ... 仅需包含文件副岛相关的头文件
```

**方案 B：提取为独立的 `FileSecondaryInputHandler` 类**

需要传入 `m_fileStash`、`m_renderer`、`m_fileSelectedIndex` 等大量状态的引用，耦合度高，收益不大。**不推荐**。

### 同时移动的辅助代码

匿名命名空间中的以下函数也一并移入新文件：

- `SecondaryKindToString()`（第 68-77 行）
- `FileHitKindToString()`（第 79-89 行）
- `PathsEqualIgnoreCase()`（第 64-66 行）

---

## Phase 3 — 调试日志条件编译（-60 行 release / 不变 debug）

**范围**：匿名命名空间中的 `AppendInputDebugLog` 和所有调用点

### 做法

用条件编译包裹：

```cpp
#ifdef _DEBUG
void AppendInputDebugLog(const std::wstring& message) { ... }
#else
inline void AppendInputDebugLog(const std::wstring&) {}
#endif
```

这样 Release 构建中所有 `AppendInputDebugLog` 调用会被编译器完全优化掉，不产生任何开销。`GetInputDebugLogPath()` 也只在 `_DEBUG` 下保留。

同时在 `HandleFileSecondaryMouseDown/Move/Up` 中，那些构建 `wostringstream` 的调试块也用 `#ifdef _DEBUG` 包裹，避免 Release 下白白构造字符串。

---

## Phase 4 — 删除死代码（-20 行）

| 位置 | 内容 | 处理 |
|------|------|------|
| 第 587-603 行 | 被注释掉的 INI 尺寸读取 | 删除 |
| 第 92-121 行 | `ExtractIconFromExe` | 全局搜索确认是否还有调用方。如果无调用方则删除 |

---

## Phase 5 — 托盘图标提取（可选，-60 行）

**范围**：`CreateTrayIcon`、`RemoveTrayIcon`、`WM_TRAYICON` 处理

### 新文件

- `src/TrayIcon.cpp`
- `include/TrayIcon.h`

### 做法

提取为独立类 `TrayIcon`，持有 `NOTIFYICONDATA`，对外暴露：

```cpp
class TrayIcon {
public:
    void Create(HWND hwnd);
    void Remove();
    // 返回选中的命令 ID，调用方处理业务逻辑
    int HandleMessage(LPARAM lParam, HWND hwnd);
};
```

`DynamicIsland` 持有 `TrayIcon m_tray;`，在 `WM_TRAYICON` 中调用 `m_tray.HandleMessage()` 并根据返回值执行 `m_settingsWindow.Toggle()` 或 `PostMessage(WM_CLOSE)`。

---

## 执行顺序与检查点

```
Phase 0  清理空行
  └─ 检查点：编译通过，运行正常
Phase 1  提取公共工具方法
  └─ 检查点：编译通过，全部交互行为不变
Phase 2  提取文件副岛交互到独立 .cpp
  └─ 检查点：编译通过，文件拖入/拖出/点击/双击/展开/折叠 全部正常
Phase 3  调试日志条件编译
  └─ 检查点：Debug 构建仍输出日志，Release 构建无日志开销
Phase 4  删除死代码
  └─ 检查点：编译通过
Phase 5  托盘图标提取（可选）
  └─ 检查点：右键托盘菜单正常
```

## 预期结果

| 阶段 | 操作 | 行数变化 |
|------|------|---------|
| Phase 0 | 清理空行 | 4211 → ~3000 |
| Phase 1 | 提取工具方法 | ~3000 → ~2910 |
| Phase 2 | 文件副岛拆分 | ~2910 → ~2660 |
| Phase 3 | 调试日志条件编译 | ~2660 → ~2600 |
| Phase 4 | 删除死代码 | ~2600 → ~2580 |
| Phase 5 | 托盘图标提取 | ~2580 → ~2520 |

最终 `DynamicIsland.cpp` 预计 **~2500 行**，且 `HandleMessage` 中的 switch 逻辑更清晰，重复代码消除。

## 需要修改的文件清单

| 文件 | 操作 |
|------|------|
| `src/DynamicIsland.cpp` | 主要瘦身目标 |
| `include/DynamicIsland.h` | 添加 `LogicalFromPhysical`、`GetProgressBarLayout` 声明 |
| `src/FileSecondaryInput.cpp` | **新建** — 文件副岛交互实现 |
| `src/TrayIcon.cpp` | **新建**（Phase 5，可选） |
| `include/TrayIcon.h` | **新建**（Phase 5，可选） |
| `DynamicIsland.vcxproj` | 添加新 .cpp/.h 到项目 |
| `DynamicIsland.vcxproj.filters` | 对应 filter 分组 |
