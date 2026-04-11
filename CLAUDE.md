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
- 主岛已改为 macOS 风格刘海，顶部贴顶，仅底部圆角。
- mini / collapsed 默认尺寸已调整为更扁的 `96x24`。
- 文件副岛输入已拆到 `src/FileSecondaryInput.cpp`。
- 文件暂存已迁入副岛体系，不再劫持主岛模式。
- `MediaMonitor` 已是 SMTC 事件优先，轮询只做兜底同步。
- 设置窗口已改为 Direct2D / DirectWrite 全自绘。
- `WM_SETTINGS_APPLY` 已扩展为热回流主题、透明度、弹簧、媒体轮询等主要设置。
- 天气设置页已支持全国城市搜索、地区联动筛选与自动定位当前位置。
- 番茄时钟已接入主岛模式系统，组件入口为 `PomodoroComponent`。
- TODO 功能已接入主岛模式系统，组件入口为 `TodoComponent`，数据层为 `TodoStore`。

## 关键文件

| 文件 | 作用 |
|------|------|
| `src/DynamicIsland.cpp` | 主窗口逻辑、消息处理、状态调度、托盘、配置回流 |
| `src/FileSecondaryInput.cpp` | 文件副岛点击 / 悬停 / 拖拽 / 双击 / 预览 |
| `src/RenderEngine.cpp` | 渲染引擎初始化、壳体绘制、组件调度 |
| `src/LayoutController.cpp` | 弹簧布局、尺寸插值、命中测试 |
| `src/SettingsWindow.cpp` | 设置窗口完整实现（D2D 自绘） |
| `src/components/PomodoroComponent.cpp` | 番茄时钟展开 / 紧凑态 UI 与交互 |
| `src/components/TodoComponent.cpp` | TODO 紧凑态入口 / 输入 / 列表 / 展开面板 |
| `src/TodoStore.cpp` | TODO 数据持久化、排序与本地存储 |
| `include/settings/SettingsControls.h` | 设置窗口控件模型 |
| `include/FileStashStore.h` | 文件暂存存储层 |
| `src/WeatherPlugin.cpp` | 和风天气 API |
| `resources/cities.json` | 天气设置页城市搜索数据源（3000+ 城市） |

计划文档统一放在 `docs/plans/`，当前包含：
- `docs/plans/REFACTOR_PLAN.md`
- `docs/plans/SLIM_PLAN.md`
- `docs/plans/NOTCH_PLAN.md`
- `docs/plans/PLAN_SETTINGS_POMODORO.md`
- `docs/plans/PLAN_WEATHER_SETTINGS.md`
- 以及设置窗口相关计划文件

## 主要功能面

- 音乐主岛：封面、标题、歌手、进度条、播放控制
- 音量副岛：仅在音乐相关场景出现
- 天气展开面板：动态天气背景 + 小时/逐日预报
- 天气设置：城市搜索、地区筛选、自动定位、LocationId 自动回填
- 提示弹窗：WiFi、蓝牙、充电、低电量等优先级调度
- 文件副岛：mini / expanded / drop target
- 设置窗口：导航、Toggle、Slider、TextInput、保存/应用
- 番茄时钟：预设时长、自定义增减、开始/暂停/终止、紧凑倒计时
- TODO：Idle 左侧入口、紧凑态快速输入、滚轮切换到 TODO 列表、展开管理面板

## 重要运行约束

- 新功能优先放进对应组件，不要继续把业务逻辑堆进 `RenderEngine`。
- 主岛壳体当前是 notch 几何，副岛仍是胶囊；不要把副岛一起改成贴顶形态。
- 所有布局坐标、命中测试、鼠标输入都要考虑 `m_dpiScale`。
- 保留 `std::round` 的 DPI 坐标换算语义，不要改成截断。
- 天气请求必须异步，UI 线程不能阻塞网络。
- 天气设置页的搜索 / 候选 / 联动筛选继续保持 D2D 自绘，不要回退到 Win32 弹出菜单或子控件。
- 文件暂存保持“真实移动”语义，目录在 `%LOCALAPPDATA%\\DynamicIsland\\FileStash\\`。
- 设置窗口保持自绘模型，不要重新引入 Win32 子控件。
- 文件副岛相关输入优先看 `src/FileSecondaryInput.cpp`，不要再把这段塞回主消息函数。
- 番茄时钟交互优先放在 `PomodoroComponent`，不要把其私有 UI 状态回塞进 `RenderEngine`。
- TODO 交互优先放在 `TodoComponent` 和 `TodoStore`，不要把 TODO 私有编辑状态回塞进 `RenderEngine`。

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
- 主题 / 透明度
- 主岛展开尺寸
- 弹簧参数
- 文件暂存上限
- 媒体轮询间隔
- 开机自启

## 开发时优先记住

- 改显示模式：先看 `DetermineDisplayMode()` 和 `TransitionTo()`
- 改布局/命中：先看 `LayoutController`
- 改文件副岛交互：先看 `FileSecondaryInput.cpp`
- 改天气 UI：优先看 `WeatherComponent`，不是 `RenderEngine`
- 改天气设置页：优先看 `SettingsWindow.cpp` + `resources/cities.json`
- 改设置页：直接看 `SettingsWindow.cpp`
- 改通知/媒体数据流：优先看 EventBus 和 monitor 层
- 改番茄时钟：优先看 `PomodoroComponent` 和 `PomodoroTimer`
- 改 TODO：优先看 `TodoComponent`、`TodoStore` 和 `DynamicIsland` 的 TODO 模式切换

## GitHub 提交约束

- 如果用户要求“提交到 GitHub / 推到远端”，默认直接提交到仓库主分支。
- 当前仓库默认主分支是 `master`；除非用户明确要求分支 / PR 工作流，否则不要默认走功能分支。
- 更新 GitHub 相关文档或自动化说明时，也继续使用“主分支 = `master`”作为当前仓库事实来源。

## 构建

统一使用 `msbuild` 进行构建，不要假设使用其他构建入口。
统一编译到默认正式输出目录 `x64\Release\DynamicIsland.exe`，不要改用其他输出文件夹。
如果 `x64\Release\DynamicIsland.exe` 被占用，先关闭正在运行的 `DynamicIsland.exe`，再重新执行默认 `msbuild` 编译。

```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
x64\Release\DynamicIsland.exe
```

## 当前建议

- 继续保持 `DynamicIsland.cpp` 不回涨，新增逻辑尽量外移
- 如继续瘦身，可评估托盘逻辑是否独立模块化
- 设置窗口暂时允许单文件收拢，必要时再做局部 helper 拆分
- 天气设置页当前功能已通路打通，后续重点应放在键盘选择、候选排序和搜索体验微调，而不是回退交互模型
- 番茄时钟功能已通路打通，后续重点应放在布局细化和图标/间距微调，而不是重做状态机
