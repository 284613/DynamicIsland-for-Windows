# 设置窗口重写计划：仿 macOS System Settings

## Context

当前 SettingsWindow 已有 macOS 双栏布局雏形（左侧边栏+右侧内容区+交通灯圆点），但使用纯 GDI 渲染，无法实现平滑动画。类别切换是瞬时销毁/重建 Win32 控件，没有过渡效果。用户要求：仿 macOS 界面、流畅功能切换、界面动效。

## 核心架构决策

**从 GDI 迁移到 Direct2D**。原因：GDI 不支持 alpha 混合、抗锯齿渐变、GPU 加速动画。项目已有完整 D2D/D3D11 栈。设置窗口使用独立的 `ID2D1HwndRenderTarget`，不依赖主岛的 `RenderEngine`。

**移除所有 Win32 子控件**（STATIC, BUTTON, TRACKBAR），全部改为 D2D 自绘 + 自定义命中测试。

## 文件清单

| 文件 | 操作 |
|------|------|
| `include/SettingsWindow.h` | 重写：移除 GDI 成员，加入 D2D 资源、Spring、控件向量 |
| `src/SettingsWindow.cpp` | 重写：D2D 渲染、动画循环、自绘控件、命中测试 |
| `include/settings/SettingsControls.h` | **新建**：控件类型枚举 + `SettingsControl` 结构体 |
| `include/Spring.h` | 不改，直接复用 |

## 当前落地状态

截至当前实现，以下内容已落地：

- 已完成 Direct2D / DirectWrite 自绘重写，设置页不再依赖 `STATIC` / `BUTTON` / `TRACKBAR`
- 已引入 `include/settings/SettingsControls.h`，并以 `SettingsControl` 驱动 Toggle / Slider / Button / TextInput
- 已完成 8 个类别页面：`General / Appearance / MainUI / FilePanel / Weather / Notifications / Advanced / About`
- 已接入内容切换过渡、滚动、DPI 适配、窗口淡入淡出
- 已加入右上角 macOS 风格红色圆形关闭按钮
- 已加入底部 `恢复默认 / 保存 / 保存并应用`
- 已切换到与主程序共用的 `config.ini`
- `ApplySettings()` 已通过 `WM_SETTINGS_APPLY` 回流主程序，刷新通知白名单、天气配置和高级参数

当前仍建议继续打磨：

- 进一步压缩设置窗口动画路径中的无效重绘
- `src/SettingsWindow_part2.cpp` 已收拢回 `src/SettingsWindow.cpp`
- 增补更多视觉细节，例如交通灯 hover 符号、主题色选择、字体缩放 UI

---

## 分步实施（11 步）

### Step 1: D2D 渲染基础

替换 GDI 双缓冲为 D2D：

- 新增成员：`ComPtr<ID2D1Factory1> m_d2dFactory`、`ComPtr<ID2D1HwndRenderTarget> m_rt`、`ComPtr<IDWriteFactory> m_dwriteFactory`
- 创建 4 个 `IDWriteTextFormat`：标题 24pt SemiBold、节标题 16pt SemiBold、正文 14pt、说明 12pt（字体 "Segoe UI"）
- 创建一组 `ID2D1SolidColorBrush`，每帧根据 dark/light 模式设色
- `WM_PAINT` → `m_rt->BeginDraw()` ... `EndDraw()`
- 删除所有 `HFONT`、GDI `DrawRoundRectFill/Stroke/PillButton/ToggleSwitch` 函数
- 删除 `WM_DRAWITEM`、`WM_CTLCOLORSTATIC`、`WM_HSCROLL` 处理

### Step 2: 控件抽象层

新建 `include/settings/SettingsControls.h`：

```cpp
enum class ControlKind { Label, Toggle, Slider, Button, Card, Separator, TextInput };

struct SettingsControl {
    ControlKind kind;
    D2D1_RECT_F bounds;
    std::wstring text;
    std::wstring subtitle;
    float value = 0;            // slider 0-1 / toggle 0|1
    float animatedValue = 0;    // 弹簧驱动的当前显示值
    Spring spring;              // 值变化弹簧
    Spring hoverSpring;         // hover 高亮弹簧
    bool enabled = true;
    bool hovered = false;
    int id = 0;
    float minVal = 0, maxVal = 1;
    std::wstring suffix;        // 显示后缀如 "%" "px"
};
```

每个页面由 `BuildXxxPage()` 返回 `std::vector<SettingsControl>`。

命中测试：在 `WM_LBUTTONDOWN/UP`、`WM_MOUSEMOVE` 中遍历 `m_activeControls`，判断点击/hover。

### Step 3: 弹簧动画循环

- `SetTimer(m_hwnd, ANIM_TIMER, 16, nullptr)` → 60fps
- `WM_TIMER`：遍历所有活跃 Spring 调用 `Update(0.016f)`，然后 `InvalidateRect`
- 当所有 Spring 已稳定 → `KillTimer`，停止渲染（0 CPU，与主岛同理）
- 需要 Spring 的地方：
  - 侧边栏选中指示器 Y 位置
  - 内容区切换 crossfade alpha × 2（出/入）
  - 内容区切换 slide offsetY × 2
  - 每个 Toggle 的 thumb 位置
  - 每个 Slider 的 knob 位置
  - 每个控件的 hover alpha
  - 滚动位置
  - 窗口打开/关闭 alpha

### Step 4: 侧边栏渲染（带动画选中指示器）

- 绘制侧边栏背景 `FillRoundedRectangle`
- 每个导航项：图标（Segoe Fluent Icons 字符）+ 文字
- 选中态：一个 `RoundedRectangle` 高亮块，其 Y 坐标由 `m_sidebarSelectionY` Spring 驱动
- 点击导航项 → `m_sidebarSelectionY.SetTarget(newY)` → 弹簧滑动到新位置
- Hover 态：每项有独立 `hoverSpring`，控制背景色 alpha

图标映射（Segoe Fluent Icons / MDL2 Assets）：
| 类别 | 图标 |
|------|------|
| 通用 | ⚙ \uE713 |
| 外观 | 🎨 \uE790 |
| 主岛 | ▢ \uE737 |
| 文件副岛 | 📁 \uE8B7 |
| 天气 | ☁ \uE9CA |
| 通知 | 🔔 \uEA8F |
| 高级 | 🔧 \uE90F |
| 关于 | ℹ \uE946 |

### Step 5: Toggle 开关 + Slider 滑块渲染

**Toggle（52×30）**：
- 轨道：`FillRoundedRectangle`，颜色在灰色/accent 蓝之间根据 `spring.GetValue()` 插值
- 滑块：24px 白色圆形，X 在左/右停靠点之间由弹簧驱动
- 点击 → 翻转 value → `spring.SetTarget(newVal)` → 启动动画

**Slider（宽 280px）**：
- 轨道：4px 高圆角矩形
- 已填充段：accent 色
- 旋钮：20px 圆形，X = lerp(left, right, animatedValue)
- hover 时旋钮放大到 22px（hoverSpring）
- 拖拽：`WM_LBUTTONDOWN` 开始 capture，`WM_MOUSEMOVE` 实时更新，`WM_LBUTTONUP` 释放
- 右侧显示数值 + 后缀

### Step 6: 类别切换 crossfade 过渡

`SwitchCategory()` 新逻辑：

1. `m_outgoingControls = std::move(m_activeControls)`
2. `m_outAlpha.SetTarget(0)`, `m_outOffsetY.SetTarget(-8)`（淡出+上移）
3. `m_activeControls = BuildNewPage()`
4. `m_inAlpha` 从 0 → 1, `m_inOffsetY` 从 +16 → 0（淡入+下滑入场）
5. 渲染时：先绘 outgoing（alpha × offset），再绘 incoming
6. outgoing alpha ≈ 0 时清空 `m_outgoingControls`
7. 弹簧参数：stiffness=120, damping=18 → 约 250ms 自然过渡

### Step 7: 卡片 + 按钮渲染

**卡片**：
- `FillRoundedRectangle` 12px 圆角
- hover 时：Y 上移 1px + 阴影加深（通过 hoverSpring 控制 alpha）
- 阴影：在卡片下方偏移 2px 绘制半透明深色圆角矩形

**底部按钮**（保存/应用）：
- 药丸形 `FillRoundedRectangle`
- Primary 按钮：accent 填充 + 白色文字
- hover 时亮度微调（hoverSpring 驱动 fillColor 插值）
- disabled 态降低不透明度

### Step 8: 内容区滚动

- `m_scrollTarget` 目标值，`m_scrollSpring` 追踪
- `WM_MOUSEWHEEL` 在内容区 → 调整 `m_scrollTarget`
- 渲染前 `m_rt->PushAxisAlignedClip(contentRect)`，然后 `SetTransform(Translation(0, -scrollY))`
- 渲染后 `PopAxisAlignedClip()` + 恢复 Transform
- 若总高度 ≤ 内容区高度 → 不启用滚动

### Step 9: 新增设置页面

扩展枚举为 8 个类别：

```cpp
enum class SettingCategory {
    General, Appearance, MainUI, FilePanel,
    Weather, Notifications, Advanced, About
};
```

**通用页（General）**：深色模式 Toggle、跟随系统 Toggle、开机自启 Toggle
**外观页（Appearance）**：主题色选择（色块网格）、字体大小
**主岛页（MainUI）**：宽度/高度/透明度 Slider（已有）
**文件副岛页（FilePanel）**：宽度/高度/透明度 Slider（已有）
**天气页（Weather）**：城市名 TextInput、API Key TextInput、位置 ID TextInput
**通知页（Notifications）**：允许的应用列表（逗号分隔 TextInput）
**高级页（Advanced）**：弹簧刚度/阻尼 Slider、低电量阈值 Slider、文件暂存上限 Slider、媒体轮询间隔 Slider
**关于页（About）**：版本信息、项目描述（只读文字）

### Step 10: 窗口打开/关闭动画

- 添加 `WS_EX_LAYERED` 窗口样式
- `Show()`：设置 `m_windowAlpha` Spring 从 0 → 1，每帧 `SetLayeredWindowAttributes(m_hwnd, 0, (BYTE)(alpha*255), LWA_ALPHA)`
- `Hide()`：`m_windowAlpha` 从 1 → 0，到 ≈0 时 `ShowWindow(SW_HIDE)`
- 移除现有 `AnimateWindow()` 调用

### Step 11: 打磨

- 交通灯圆点 hover 显示 ×/−/＋ 符号（hoverSpring 控制 alpha）
- 侧边栏分组标题（"PREFERENCES" / "ADVANCED"）
- Header 区状态药丸淡入/淡出
- Tab 键盘导航 + 焦点环（2px accent 描边）
- `WM_DPICHANGED` 处理：重建 render target + 重算布局
- TextInput 控件：光标闪烁、选中高亮、基础文本编辑

---

## 设置持久化扩展

新增 INI 段落（写入 `config.ini`）：

```ini
[Weather]
City=北京
District=朝阳
APIKey=xxx
LocationId=101010100

[Notifications]
AllowedApps=微信,QQ

[Advanced]
SpringStiffness=100
SpringDamping=10
LowBatteryThreshold=20
FileStashMaxItems=5
MediaPollIntervalMs=1000
```

`ApplySettings()` → `PostMessage(m_parentHwnd, WM_SETTINGS_APPLY, 0, 0)` → `DynamicIsland` 处理该消息，重新读取配置并通知各模块。

## 验证方法

1. 编译运行 `msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64`
2. 右键托盘图标 → 设置 → 窗口应以淡入动画出现
3. 点击侧边栏各项 → 选中指示器平滑滑动，内容区 crossfade 过渡
4. 操作 Toggle → 滑块平滑滑动
5. 拖动 Slider → 旋钮跟随，数值实时更新
6. Hover 卡片 → 微浮起 + 阴影变化
7. 内容超出时滚轮滚动 → 弹性滚动
8. ESC 关闭 → 淡出动画
9. 修改设置并应用 → 主岛实时响应变化
10. 修改任意设置后，底部按钮应立即可点击；点击 `恢复默认` 后当前页值应立刻回退
11. 点击右上角红色圆形关闭按钮 → 设置窗口关闭
