# Dynamic Island for Windows

一个基于 C++17、Win32 API、Direct2D、DirectComposition 和 D3D11 的 Windows 桌面灵动岛应用。它把音乐播放、逐字歌词、天气、通知、Todo、Pomodoro、Agent 会话、文件暂存和人脸解锁整合到顶部主岛与轻量副岛中。

## 功能概览

- 音乐岛：支持紧凑态和展开态，包含封面、黑胶唱片样式、唱臂、进度、控制按钮和波形律动。
- 逐字歌词：优先使用网易云 YRC 动态歌词，缺失时回退 KLYRIC/LRC 近似逐字高亮。
- 收缩态：紧凑态可手动收缩到顶部细线，点击细线恢复；收缩 HUD 只显示摘要并尽量保持点击穿透。
- 天气：天气 API Key 只从配置读取，未配置时显示 unavailable 状态，不再使用内置 fallback。
- Todo：支持紧凑态列表、快速输入、简化展开态和本地持久化。
- Pomodoro：支持紧凑/展开态、暂停恢复和本地快照。
- 文件暂存：拖入主岛时显示吞食动效，右侧显示单文件圆圈；暂存索引支持启动恢复。
- Agent：支持 Claude/Codex 会话面板、hook 事件刷新和空闲降频轮询。
- 人脸解锁：基于 ML 的 Windows 锁屏人脸识别（YuNet/ArcFace/Silent-Face/3DDFA_V2），支持自动登录，包含平滑聚拢与从左至右 3D 翻转打勾的灵动岛解锁动画反馈。
- 通知：监听系统通知并限制已处理缓存增长。
- 设置页：Direct2D 自绘设置窗口，支持主题、透明度、天气、人脸录入、紧凑态顺序、专辑样式等配置热更新。

## 技术栈

- 语言：C++17
- 图形：Direct2D 1.1、DirectComposition、D3D11
- 系统接口：Win32 API、WinRT、WASAPI、SMTC、Media Foundation
- 机器学习：ONNX Runtime 1.17+
- 构建：Visual Studio 2022 / MSBuild

## 架构

```text
输入层: MediaMonitor / NotificationMonitor / WeatherPlugin / SystemMonitor / AgentSessionMonitor / FaceUnlockBridge
调度层: DynamicIsland -> LayoutController
渲染层: RenderEngine -> src/components/
底层能力: face_core (ML Pipeline) / FaceUnlockProvider (Credential Provider DLL)
```

- `DynamicIsland`：窗口消息、状态机、输入分发、配置热更新和模式切换。
- `LayoutController`：尺寸、透明度、弹簧动画和命中测试。
- `RenderEngine`：Direct2D/DirectComposition 生命周期、壳体绘制和组件调度。
- `src/components/`：音乐、歌词、天气、Todo、Pomodoro、文件圆圈、Agent、人脸反馈等具体 UI。
- `face_core`：封装摄像头采集与多级人脸识别算法流水线。

## 目录结构

```text
DynamicIsland/
├─ src/
│  ├─ components/             # 独立 UI 组件
│  ├─ DynamicIsland.cpp       # 主窗口消息与状态调度
│  ├─ FaceEnrollWindow.cpp    # 人脸录入引导窗口
│  ├─ FaceUnlockBridge.cpp    # 桥接锁屏解锁事件
│  ├─ RenderEngine.cpp        # 渲染壳体与组件调度
│  └─ LayoutController.cpp    # 弹簧布局与命中测试
├─ include/
│  ├─ components/
│  ├─ settings/
│  └─ *.h
├─ face_core/                 # 人脸识别核心静态库
├─ FaceUnlockProvider/        # 凭据提供程序 DLL (CP)
├─ resources/
├─ models/                    # ONNX 模型文件
├─ docs/
│  └─ plans/
├─ DynamicIsland.sln
└─ DynamicIsland.vcxproj
```

## 配置

应用读取 exe 同目录下的 `config.ini`。常用配置节：

- `[Settings]`
- `[MainUI]`
- `[FilePanel]`
- `[Weather]`
- `[FaceUnlock]`
- `[Notifications]`

关键配置说明：

- `Weather.ApiKey` 需要用户自行配置；未配置时天气模块不会发起请求。
- `FaceUnlock.Enabled` 控制是否启用锁屏识别（需要管理员权限安装 CP）。

## 本地数据

- Todo：`%LOCALAPPDATA%\DynamicIsland\todos.json`
- 文件暂存：`%LOCALAPPDATA%\DynamicIsland\FileStash\`
- 人脸模板：`%PROGRAMDATA%\DynamicIsland\faces.bin` (加密存储)
- 人脸凭据：通过 Windows LSA Secrets 安全存储

## 构建与运行

环境要求：

- Windows 10/11 x64
- Visual Studio 2022
- Windows SDK 10.0
- 已下载模型文件至 `models/` 目录

构建命令：

```powershell
msbuild DynamicIsland.sln /p:Configuration=Debug /p:Platform=x64
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
```

安装人脸解锁：
以管理员权限运行 `DynamicIsland.exe --install-cp`。

运行：

```powershell
x64\Release\DynamicIsland.exe
```

如果构建 Release 时提示 exe 被占用，先关闭正在运行的 `DynamicIsland.exe`。

## 开发入口

- 改显示模式：先看 `DetermineDisplayMode()`、`TransitionTo()` 和 `ActiveExpandedMode`。
- 改布局/命中：先看 `LayoutController`，同时确认 DPI 缩放。
- 改人脸岛动画：看 `src/components/FaceIdComponent.cpp`。
- 改人脸识别逻辑：看 `face_core/src/FacePipeline.cpp`。
- 改人脸录入：看 `src/FaceEnrollWindow.cpp`。
- 改设置页：看 `src/SettingsWindow.cpp`。

## 文档

- 人脸解锁实现方案：`docs/FaceUnlockPlan.md`
- 当前优化状态：`docs/OPT_STATUS.md`
- 下一批功能路线图：`docs/plans/2026-04-23-next-batch-roadmap.md`

## 许可

本项目仅供学习与研究使用。
