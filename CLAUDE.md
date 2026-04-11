# CLAUDE.md - Dynamic Island for Windows

## 项目定位

把 iPhone Dynamic Island 体验移植到 Windows 10/11 桌面。

- 技术栈：C++17 / Win32 API / Direct2D 1.1 / DirectComposition / D3D11
- 构建：Visual Studio 2022 + MSBuild
- 核心机制：EventBus、弹簧动画、DirtyFlags 按需渲染、多岛 Region 合并

## 一句话架构

```text
输入层  MediaMonitor / NotificationMonitor / WeatherPlugin / SystemMonitor
调度层  DynamicIsland -> LayoutController
渲染层  RenderEngine + src/components/
```

职责边界：
- `DynamicIsland` 负责窗口消息、状态机、输入分发、配置回流、模式切换。
- `LayoutController` 负责尺寸、透明度、弹簧目标、命中测试。
- `RenderEngine` 只负责 D2D/DComp 生命周期、壳体和组件调度。
- 具体 UI 业务放在 `src/components/`，不要把业务私有状态重新塞回 `RenderEngine`。

## 当前实现状态

- 组件化重构已完成，`RenderEngine.cpp` 已瘦身为壳体 + 调度。
- `DynamicIsland.cpp` 已做一轮 slim，当前约 `1831` 行。
- 文件副岛输入已拆到 `src/FileSecondaryInput.cpp`。
- 文件暂存已迁入副岛体系，不再劫持主岛模式。
- `MediaMonitor` 已是 SMTC 事件优先，轮询只做兜底同步。
- 设置窗口已改为 Direct2D / DirectWrite 全自绘。

## 关键文件

| 文件 | 作用 |
|------|------|
| `src/DynamicIsland.cpp` | 主窗口逻辑、消息处理、状态调度、托盘、配置回流 |
| `src/FileSecondaryInput.cpp` | 文件副岛点击 / 悬停 / 拖拽 / 双击 / 预览 |
| `src/RenderEngine.cpp` | 渲染引擎初始化、壳体绘制、组件调度 |
| `src/LayoutController.cpp` | 弹簧布局、尺寸插值、命中测试 |
| `src/SettingsWindow.cpp` | 设置窗口完整实现（D2D 自绘） |
| `include/settings/SettingsControls.h` | 设置窗口控件模型 |
| `include/FileStashStore.h` | 文件暂存存储层 |
| `src/WeatherPlugin.cpp` | 和风天气 API |

## 主要功能面

- 音乐主岛：封面、标题、歌手、进度条、播放控制
- 音量副岛：仅在音乐相关场景出现
- 天气展开面板：动态天气背景 + 小时/逐日预报
- 提示弹窗：WiFi、蓝牙、充电、低电量等优先级调度
- 文件副岛：mini / expanded / drop target
- 设置窗口：导航、Toggle、Slider、TextInput、保存/应用

## 重要运行约束

- 新功能优先放进对应组件，不要继续把业务逻辑堆进 `RenderEngine`。
- 所有布局坐标、命中测试、鼠标输入都要考虑 `m_dpiScale`。
- 保留 `std::round` 的 DPI 坐标换算语义，不要改成截断。
- 天气请求必须异步，UI 线程不能阻塞网络。
- 文件暂存保持“真实移动”语义，目录在 `%LOCALAPPDATA%\\DynamicIsland\\FileStash\\`。
- 设置窗口保持自绘模型，不要重新引入 Win32 子控件。
- 文件副岛相关输入优先看 `src/FileSecondaryInput.cpp`，不要再把这段塞回主消息函数。

## 配置

统一写入 exe 同目录 `config.ini`。

常用节：
- `[Settings]`
- `[MainUI]`
- `[FilePanel]`
- `[Weather]`
- `[Notifications]`
- `[Advanced]`

`WM_SETTINGS_APPLY` 当前会回流：
- 通知白名单
- 天气配置
- 低电量阈值

## 开发时优先记住

- 改显示模式：先看 `DetermineDisplayMode()` 和 `TransitionTo()`
- 改布局/命中：先看 `LayoutController`
- 改文件副岛交互：先看 `FileSecondaryInput.cpp`
- 改天气 UI：优先看 `WeatherComponent`，不是 `RenderEngine`
- 改设置页：直接看 `SettingsWindow.cpp`
- 改通知/媒体数据流：优先看 EventBus 和 monitor 层

## 构建

```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
x64\Release\DynamicIsland.exe
```

## 当前建议

- 继续保持 `DynamicIsland.cpp` 不回涨，新增逻辑尽量外移
- 如继续瘦身，可评估托盘逻辑是否独立模块化
- 设置窗口暂时允许单文件收拢，必要时再做局部 helper 拆分
