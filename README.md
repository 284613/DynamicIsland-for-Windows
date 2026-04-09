# Dynamic Island for Windows

> 将 iPhone 灵动岛体验移植到 Windows 桌面

## 项目简介

Dynamic Island for Windows 是一款基于 C++/Win32 API 和 Direct2D 构建的 Windows 桌面应用，旨在为 Windows 用户带来类似 iPhone 14 Pro 灵动岛的交互体验。

应用悬浮于屏幕顶部区域，以紧凑的胶囊形态呈现，能够智能检测并展示系统任务状态（音乐播放、通知提醒等），同时支持丰富的功能扩展。

---

## 功能列表

### 核心功能

| 功能模块 | 说明 |
|---------|------|
| **按需渲染引擎** | [HOT] 核心重构：引入 DirtyFlags 标脏机制。空闲态定时器完全停止，实现 0% CPU 占用，仅在有变化或动画时渲染。 |
| **音乐播放器组件** | 显示专辑封面、歌曲名、歌手、播放进度条，支持播放/暂停/上一曲/下一曲控制 |
| **多岛交互模式** | [NEW] 支持多岛协同显示（如音乐展开时调节音量，下方弹出独立副岛），含平滑弹簧动画 |
| **媒体封面缓存** | [NEW] 专辑封面支持本地磁盘缓存，实现切歌瞬间秒开，无抓取延迟 |
| **媒体会话监控** | 自动检测 Spotify、网易云音乐等主流音乐软件的播放状态（WinRT SMTC 会话） |
| **歌词监控** | 支持网易云音乐歌词获取与实时滚动显示（含本地缓存与弹簧动画） |
| **音量调节优化** | 仅在播放音乐时允许调节，展开态支持副岛反馈，避免全局误触 |
| **提示弹窗组件** | WiFi、蓝牙、充电、低电量等场景提示，含 P0-P3 优先级调度 |
| **系统监控** | 实时电量、网络连接、全屏应用检测自动隐藏 |
| **天气插件** | 和风天气数据源，API Key 从 `config.ini` 读取。展开面板左卡为**意境动态背景**（雨/雷/雪/晴/多云/雾各有独立动画），右卡逐小时预报（时间 + 矢量图标 + 温度）。鼠标滚轮在展开面板内切换**逐小时 ↔ 逐日（7天）**预报视图。多云动画：晴空中云朵从侧面飘入遮天，入场后持续风吹漂移 |
| **文件中转站** | D2D 高性能拖放组件，支持系统图标识别与流体交互 |

---

## 技术架构

### 技术栈

- **语言**: C++17
- **图形渲染**: Direct2D 1.1 + DirectComposition + D3D11
- **系统接口**: Win32 API, WinRT (Media/Notifications), WASAPI
- **构建系统**: MSBuild / Visual Studio 2022

### 项目结构 (Standardized C++ Structure)

```
E:\vs\c++\DynamicIsland\
├── src/                        # 源代码实现 (.cpp)
│   ├── components/             # UI 组件实现 (Music, Alert, Volume, FilePanel)
│   ├── main.cpp                # 程序入口
│   ├── DynamicIsland.cpp       # 主窗口逻辑与状态机
│   ├── RenderEngine.cpp        # Direct2D 渲染引擎
│   ├── LayoutController.cpp    # 弹簧布局控制器
│   └── ...                     # 其他监控与逻辑模块
│
├── include/                    # 头文件声明 (.h)
│   ├── components/             # UI 组件头文件
│   ├── DynamicIsland.h         # 主类定义
│   ├── IslandState.h           # 渲染上下文与状态枚举
│   ├── EventBus.h              # 事件驱动核心
│   ├── Spring.h                # 弹簧物理引擎
│   └── ...                     # 其他模块定义
│
├── resources/                  # 资源文件
│   ├── icon/                   # 系统图标与素材
│   └── Dynamic Island.rc       # 资源脚本
│
├── docs/                       # 项目文档与优化方案
├── vcxproj/                    # Visual Studio 项目配置副本
├── DynamicIsland.sln           # 解决方案
└── DynamicIsland.vcxproj       # 项目文件
```

---

## 快速开始

### 环境要求

- Windows 10/11 x64
- Visual Studio 2022 (v143)
- Windows SDK 10.0

### 编译与运行

1. 使用 Visual Studio 打开 `DynamicIsland.sln`。
2. 选择 **Release** 配置与 **x64** 平台。
3. 点击 **生成解决方案**。
4. 运行生成的 `x64\Release\DynamicIsland.exe`。

---

## 许可证

本项目仅供学习与研究使用。
