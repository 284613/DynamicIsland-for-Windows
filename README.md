# Dynamic Island for Windows

一个基于 C++17、Win32 API、Direct2D、DirectComposition 和 D3D11 的 Windows 桌面灵动岛应用。它把音乐播放、逐字歌词、天气、通知、Todo、Pomodoro、Agent 会话、文件暂存和人脸解锁整合到顶部主岛与轻量副岛中。

## 功能概览

- **音乐岛**：支持紧凑态和展开态（240px），包含大黑胶唱片样式、黑胶环形声波脉动（取代长条波形）、进度、五按钮控制组和多行歌词同步。
- **逐字歌词**：优先使用网易云 YRC 动态歌词，支持 `tlyric` 行级翻译对齐，提供四种翻译显示模式（关闭/仅当前句/每行/仅译文）。
- **天气重构**：支持 2x3 比例的 6 小时预报网格，新增根据天气类型动态变化的意境背景（晴天/雨天/阴天等），优化了 API Key 读取逻辑。
- **收缩态**：紧凑态可手动收缩到顶部细线，点击细线恢复；收缩 HUD 只显示摘要并尽量保持点击穿透。
- **人脸解锁**：基于 ML 的 Windows 锁屏人脸识别（YuNet/ArcFace/Silent-Face/3DDFA_V2），支持自动登录，包含平滑聚拢与从左至右 3D 翻转打勾的灵动岛解锁动画反馈。
- **文件暂存**：拖入主岛时显示吞食动效，右侧显示单文件圆圈；暂存索引支持启动恢复。
- **Todo & Pomodoro**：支持紧凑/展开态、快速输入、本地持久化与快照恢复。
- **Agent**：支持 Claude/Codex 会话面板、hook 事件刷新和空闲降频轮询。
- **设置页**：Direct2D 自绘设置窗口，支持主题、透明度、天气、人脸录入、音乐样式、歌词翻译模式等配置热更新。

## 技术栈

- **语言**：C++17 (MSVC)
- **图形**：Direct2D 1.1、DirectComposition、D3D11、DirectWrite
- **系统接口**：Win32 API、WinRT、WASAPI、SMTC、Media Foundation
- **机器学习**：ONNX Runtime 1.17+
- **构建**：Visual Studio 2022 / MSBuild

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
- `src/components/`：音乐（黑胶/环形脉动/五按钮）、歌词（三行/翻译）、天气（意境背景/2x3网格）、Todo、Pomodoro、文件圆圈、Agent、人脸反馈等具体 UI。
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
│  ├─ plans/                  # 开发与重构计划
│  └─ music_mockups/          # 音乐重构视觉稿
├─ DynamicIsland.sln
└─ DynamicIsland.vcxproj
```

## 配置

应用读取 exe 同目录下的 `config.ini`。常用配置节：

- `[Settings]`
- `[MainUI]`：包含 `CompactAlbumArtStyle`、`ExpandedAlbumArtStyle`、`LyricTranslationMode` 和 `VinylRingPulse` 等。
- `[FilePanel]`
- `[Weather]`：`ApiKey` 需要在此配置。
- `[FaceUnlock]`
- `[Notifications]`

## 音乐岛与歌词

- **视觉重构**：展开态高度提升至 240px，采用左侧大黑胶 + 右侧三行歌词布局。
- **环形脉动**：黑胶外圈根据播放状态产生 2-3 圈半透明白色描边脉动（振幅随音频信号变化）。
- **翻译增强**：支持解析并显示网易云 `tlyric`。翻译模式可在设置中热切换：`Off` / `CurrentOnly` / `AllLines` / `TranslationOnly`。
- **紧凑态自适应**：有歌词/翻译时，紧凑态高度会自动从 64px 扩展至 76px。

## 天气重构

- **意境背景**：展开态左侧卡片根据实时天气类型显示不同的 ambient 背景动效。
- **2x3 网格**：右侧卡片显示未来 6 小时的逐小时预报，布局更加紧凑直观。
- **性能优化**：渲染逻辑大幅精简，提升了 60fps 环境下的绘制稳定性。

## 本地数据

- Todo：`%LOCALAPPDATA%\DynamicIsland\todos.json`
- 文件暂存：`%LOCALAPPDATA%\DynamicIsland\FileStash\`
- 人脸模板：`%PROGRAMDATA%\DynamicIsland\faces.bin` (加密存储)
- 人脸凭据：通过 Windows LSA Secrets 安全存储

## 构建与运行

环境要求：
- Windows 10/11 x64
- Visual Studio 2022 / Windows SDK 10.0
- 已下载模型文件至 `models/` 目录

构建：
```powershell
msbuild DynamicIsland.sln /p:Configuration=Release /p:Platform=x64
```

安装人脸解锁：
以管理员权限运行 `DynamicIsland.exe --install-cp`。

## 文档

- 音乐重构计划：`docs/plans/2026-04-28-music-restyle-plan.md`
- 人脸解锁实现方案：`docs/FaceUnlockPlan.md`
- 当前优化状态：`docs/OPT_STATUS.md`
- 下一批功能路线图：`docs/plans/2026-04-23-next-batch-roadmap.md`
- 代码瘦身计划：`docs/plans/SLIM_PLAN.md`

## 许可

本项目仅供学习与研究使用。
