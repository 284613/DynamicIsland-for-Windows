# CLAUDE.md

## 项目定位

Dynamic Island for Windows 是一个基于 C++17、Win32 API、Direct2D、DirectComposition、D3D11 的桌面灵动岛应用。核心目标是把音乐、歌词、天气、通知、Todo、Pomodoro、Agent 会话、文件暂存和人脸解锁整合到顶部主岛与轻量副岛中。

## 工作方式

- 先理解再修改：改代码前阅读相关文件和现有模式。
- 输出保持简洁，但推理和实现要完整。
- 优先小范围编辑，不要无理由重写大文件。
- 已读且未变化的文件不要反复读取；超过 100KB 的文件除非必要不要打开。
- 方案保持简单直接，优先复用现有组件和工具。
- 完成前必须构建或运行最小有效验证。
- 不使用讨好式开头或空泛结尾。
- 用户的明确指令优先于本文件。
- 长会话或任务切换明显时，建议用户开启新会话；必要时提醒使用 `/cost` 查看成本。

## 架构边界

```text
输入层: MediaMonitor / NotificationMonitor / WeatherPlugin / SystemMonitor / AgentSessionMonitor / FaceUnlockBridge
调度层: DynamicIsland -> LayoutController
渲染层: RenderEngine -> src/components/
底层能力: face_core (Camera/ML) / FaceUnlockProvider (Credential Provider DLL)
```

- `DynamicIsland` 负责窗口消息、状态机、输入分发、配置热更新和模式切换。
- `LayoutController` 负责尺寸、透明度、弹簧动画和命中测试。
- `RenderEngine` 只负责 Direct2D/DirectComposition 生命周期、壳体绘制和组件调度。
- `face_core` 是独立静态库，处理摄像头、YuNet 识别、ArcFace 比对、Silent-Face 活体和 DPAPI/LSA 存储。
- `FaceUnlockProvider` 是 Windows 凭据提供程序 DLL，加载 `face_core` 实现锁屏自动进入。
- 具体 UI 逻辑放在 `src/components/`，不要把业务私有状态塞回 `RenderEngine`。

## 当前重点能力

- 音乐紧凑态和展开态：封面、黑胶样式、歌词、波形、进度和播放控制。
- 歌词：优先解析网易云 YRC，缺失时回退 KLYRIC/LRC 近似逐字。
- 收缩态：手动收缩、顶部细线唤醒、HUD 摘要显示和点击穿透转发。
- 文件暂存：主岛吞食动效、右侧单文件圆圈、启动恢复。
- Todo：紧凑态列表、快速输入、简化展开态 and 本地持久化。
- Pomodoro：紧凑/展开状态、暂停恢复和本地快照。
- 天气：只读取配置中的 API Key；未配置时保持 unavailable，不发起请求。
- Agent：Claude/Codex 会话监控、hook 刷新和自适应轮询。
- 人脸解锁：基于 ML 的 5 层流水线（检测、对齐、比对、活体、姿态），支持锁屏自动登录，包含平滑聚拢与从左至右 3D 翻转打勾的灵动岛解锁动画反馈。
- 人脸录入：Direct2D 引导式录入，支持多角度模板捕捉。

## 修改入口

- 显示模式和状态流转：`src/DynamicIsland.cpp`
- 布局和命中测试：`src/LayoutController.cpp`
- 音乐视觉：`src/components/MusicPlayerComponent.cpp`
- 波形：`src/components/WaveformComponent.cpp`
- 歌词解析：`src/LyricsMonitor.cpp`
- 歌词绘制：`src/components/LyricsComponent.cpp`
- 文件圆圈/吞食：`src/components/FilePanelComponent.cpp`、`src/FileSecondaryInput.cpp`
- Todo：`src/components/TodoComponent.cpp`、`src/TodoStore.cpp`
- 设置页：`src/SettingsWindow.cpp`
- 天气：`src/WeatherPlugin.cpp`、`src/components/WeatherComponent.cpp`
- 人脸逻辑：`face_core/` (底层)、`src/FaceUnlockBridge.cpp` (桥接)
- 人脸录入窗口：`src/FaceEnrollWindow.cpp`
- 人脸岛动画：`src/components/FaceIdComponent.cpp`
- 凭据提供程序：`FaceUnlockProvider/`

## 开发约束

- 新功能优先进入对应组件，避免继续放大 `DynamicIsland.cpp` 和 `RenderEngine.cpp`。
- 所有坐标、窗口 region、命中测试和拖拽转发都要考虑 `m_dpiScale`。
- DPI 坐标转换保持现有 `std::round` 语义，不要改成截断。
- 天气、歌词、Agent、通知、人脸识别等后台任务不能阻塞 UI 线程。
- 天气请求禁止内置 API Key fallback。
- 设置窗口保持 Direct2D 自绘，不要退回 Win32 子控件堆叠。
- 文件暂存保持真实移动语义，路径在 `%LOCALAPPDATA%\DynamicIsland\FileStash\`。
- **安全约束**：人脸凭据必须通过 `LsaStorePrivateData` 存储，禁止明文或简单混淆。
- 不新增第三方依赖，除非用户明确要求。

## 构建与运行

```powershell
# 构建全方案
msbuild DynamicIsland.sln /p:Configuration=Debug /p:Platform=x64
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64

# 运行
x64\Release\DynamicIsland.exe
```

如果 Release 可执行文件被占用，先关闭正在运行的 `DynamicIsland.exe` 再构建。
安装凭据提供程序需要管理员权限运行 `DynamicIsland.exe --install-cp`。

## GitHub 提交

- 本仓库默认主分支是 `master`。
- 用户要求“提交到 GitHub / 推送到远端”时，默认提交并推送当前分支；除非用户明确要求 PR 或新分支。
- 提交前检查 `git status -sb`，不要把 `.omx/`、`.claude/` 本地运行时状态或无关代理文件混入提交。
- 提交信息要说明为什么改，而不只是列出改了什么。
