# CLAUDE.md - Dynamic Island for Windows

## 项目概览

将 iPhone 14 Pro 灵动岛体验移植到 Windows 10/11 桌面。

- 技术栈：C++17 / Win32 API / Direct2D 1.1 + DirectComposition + D3D11 / MSBuild VS2022
- 核心机制：EventBus、弹簧物理、DirtyFlags 按需渲染、Win32 Region 多岛合并

## 架构

```text
输入层  MediaMonitor / NotificationMonitor / WeatherPlugin / SystemMonitor
处理层  TaskDetector -> LayoutController -> DynamicIsland
渲染层  RenderEngine + src/components/
```

说明：
- `DynamicIsland` 负责状态机、输入、调度、配置回流。
- `RenderEngine` 只负责 D2D 生命周期、主/副岛壳体、组件调度。
- 业务 UI 在 `src/components/`，不要把业务状态重新塞回 `RenderEngine`。

## 关键文件

| 文件 | 作用 |
|------|------|
| `src/DynamicIsland.cpp` | 主窗口逻辑、消息处理、配置加载、托盘菜单 |
| `src/RenderEngine.cpp` | D2D/DComp 初始化与组件调度 |
| `src/SettingsWindow.cpp` | 设置窗口完整实现：资源、页面构建、配置读写、渲染、输入、WndProc |
| `include/settings/SettingsControls.h` | 设置窗口控件模型 |
| `include/FileStashStore.h` | 文件暂存存储层 |
| `src/WeatherPlugin.cpp` | 和风天气 API |
| `src/LayoutController.cpp` | 布局弹簧与尺寸插值 |

## 当前实现状态

### 主岛 / 组件

- 组件化已完成，`RenderEngine.cpp` 已瘦身到只保留壳体与调度。
- 文件暂存已迁入副岛体系，不再劫持主岛模式。
- `MediaMonitor` 已改成 SMTC 事件优先，轮询只做兜底。

### 设置窗口

- 已从 GDI 改为 Direct2D / DirectWrite 全自绘。
- 不再依赖 `STATIC` / `BUTTON` / `TRACKBAR` 子控件。
- 当前类别：
  - `General`
  - `Appearance`
  - `MainUI`
  - `FilePanel`
  - `Weather`
  - `Notifications`
  - `Advanced`
  - `About`
- 当前能力：
  - 页面切换过渡
  - 内容区滚动
  - Toggle / Slider / TextInput / 自绘按钮
  - 右上角 macOS 风格红色圆形关闭按钮
  - `恢复默认 / 保存 / 保存并应用`

## 配置

统一写入 exe 同目录 `config.ini`。

主要键：

```ini
[Settings]
DarkMode=0
FollowSystemTheme=1
AutoStart=0

[MainUI]
Width=400
Height=420
Transparency=100

[FilePanel]
Width=340
Height=200
Transparency=90

[Weather]
City=北京
APIKey=
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

`ApplySettings()` 通过 `WM_SETTINGS_APPLY` 回流到 `DynamicIsland`，当前会刷新：
- 通知白名单
- 天气配置
- 低电量阈值

## 开发规则

- 新功能优先放到对应组件，不要让 `RenderEngine` 再背业务状态。
- 布局坐标和输入命中要考虑 `m_dpiScale`。
- 天气请求必须异步，UI 线程不要阻塞网络。
- 文件暂存语义保持真实移动，目录在 `%LOCALAPPDATA%\\DynamicIsland\\FileStash\\`。
- 设置窗口继续保持自绘模型，不要重新引入 Win32 子控件。

## Build

```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
x64\Release\DynamicIsland.exe
```

## 后续建议

- 继续保持 `src/SettingsWindow.cpp` 的单文件收拢状态，并在需要时再做局部 helper 拆分
- 继续压缩设置窗口拖动/切页时的无效重绘
- 补齐外观页主题色、字体缩放等 UI
