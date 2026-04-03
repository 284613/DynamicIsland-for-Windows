const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType, HeadingLevel, BorderStyle, WidthType,
  ShadingType, VerticalAlign, PageNumber, PageBreak, LevelFormat,
  TableOfContents, ImageRun
} = require('docx');
const fs = require('fs');
const path = require('path');

// 颜色定义
const COLORS = {
  primary: '1F4E79',      // 深蓝
  secondary: '2E75B6',    // 中蓝
  accent: 'D5E8F0',       // 浅蓝背景
  dark: '1A1A1A',         // 深灰文字
  gray: '595959',         // 中灰
  lightGray: 'F2F2F2',    // 浅灰背景
  white: 'FFFFFF',
  code: 'F8F8F8',
  border: 'CCCCCC'
};

const PAGE_WIDTH = 12240;  // US Letter
const PAGE_HEIGHT = 15840;
const MARGIN = 1080;       // 0.75 inch
const CONTENT_WIDTH = PAGE_WIDTH - MARGIN * 2;

// 创建边框样式
const cellBorder = (color = COLORS.border) => ({
  top: { style: BorderStyle.SINGLE, size: 1, color },
  bottom: { style: BorderStyle.SINGLE, size: 1, color },
  left: { style: BorderStyle.SINGLE, size: 1, color },
  right: { style: BorderStyle.SINGLE, size: 1, color }
});

// 创建段落间距
const spacing = (before = 0, after = 120, line = null) => ({
  before, after, ...(line ? { line, lineRule: "auto" } : {})
});

// 创建 TextRun
const t = (text, opts = {}) => new TextRun({ text, font: 'Arial', size: 22, ...opts });
const tb = (text, opts = {}) => t(text, { bold: true, ...opts });

// 创建标题
const heading = (level, text) => {
  const configs = {
    1: { size: 36, bold: true, color: COLORS.primary, before: 360, after: 180, outline: 0 },
    2: { size: 28, bold: true, color: COLORS.secondary, before: 280, after: 140, outline: 1 },
    3: { size: 24, bold: true, color: COLORS.dark, before: 200, after: 100, outline: 2 },
    4: { size: 22, bold: true, color: COLORS.gray, before: 160, after: 80, outline: 3 }
  };
  const cfg = configs[level];
  return new Paragraph({
    heading: level === 1 ? HeadingLevel.HEADING_1 :
             level === 2 ? HeadingLevel.HEADING_2 :
             level === 3 ? HeadingLevel.HEADING_3 : HeadingLevel.HEADING_4,
    spacing: spacing(cfg.before, cfg.after),
    children: [new TextRun({
      text,
      font: 'Arial',
      size: cfg.size,
      bold: cfg.bold,
      color: cfg.color
    })]
  });
};

// 创建段落
const p = (children, opts = {}) => {
  const items = typeof children === 'string' ? [t(children)] : children;
  return new Paragraph({
    spacing: spacing(80, 80),
    children: items,
    ...opts
  });
};

// 创建代码块段落
const codeLine = (text) => new Paragraph({
  spacing: spacing(0, 0),
  indent: { left: 360 },
  children: [new TextRun({
    text,
    font: 'Consolas',
    size: 18,
    color: COLORS.dark
  })]
});

// 创建列表项
const bullet = (text, level = 0) => new Paragraph({
  numbering: { reference: 'bullets', level },
  spacing: spacing(40, 40),
  children: [t(text)]
});

const numbered = (text, level = 0) => new Paragraph({
  numbering: { reference: 'numbers', level },
  spacing: spacing(40, 40),
  children: [t(text)]
});

// 创建简单表格
const simpleTable = (headers, rows, colWidths) => {
  const totalWidth = colWidths.reduce((a, b) => a + b, 0);

  const headerRow = new TableRow({
    tableHeader: true,
    children: headers.map((h, i) => new TableCell({
      borders: cellBorder(COLORS.secondary),
      width: { size: colWidths[i], type: WidthType.DXA },
      shading: { fill: COLORS.secondary, type: ShadingType.CLEAR },
      margins: { top: 80, bottom: 80, left: 120, right: 120 },
      children: [new Paragraph({
        spacing: spacing(0, 0),
        children: [new TextRun({ text: h, font: 'Arial', size: 20, bold: true, color: COLORS.white })]
      })]
    }))
  });

  const dataRows = rows.map(row => new TableRow({
    children: row.map((cell, i) => new TableCell({
      borders: cellBorder(),
      width: { size: colWidths[i], type: WidthType.DXA },
      margins: { top: 60, bottom: 60, left: 120, right: 120 },
      children: [new Paragraph({
        spacing: spacing(0, 0),
        children: [t(cell)]
      })]
    }))
  }));

  return new Table({
    width: { size: totalWidth, type: WidthType.DXA },
    columnWidths: colWidths,
    rows: [headerRow, ...dataRows]
  });
};

// 创建代码表格
const codeTable = (code, width = CONTENT_WIDTH) => {
  const lines = code.trim().split('\n');
  return new Table({
    width: { size: width, type: WidthType.DXA },
    columnWidths: [width],
    rows: lines.map(line => new TableRow({
      children: [new TableCell({
        borders: cellBorder(COLORS.border),
        width: { size: width, type: WidthType.DXA },
        shading: { fill: COLORS.code, type: ShadingType.CLEAR },
        margins: { top: 40, bottom: 40, left: 120, right: 120 },
        children: [new Paragraph({
          spacing: spacing(0, 0),
          children: [new TextRun({
            text: line,
            font: 'Consolas',
            size: 16,
            color: COLORS.dark
          })]
        })]
      })]
    }))
  });
};

// ==================== 文档内容 ====================

const doc = new Document({
  numbering: {
    config: [
      {
        reference: 'bullets',
        levels: [
          { level: 0, format: LevelFormat.BULLET, text: '\u2022', alignment: AlignmentType.LEFT,
            style: { paragraph: { indent: { left: 480, hanging: 240 } } } },
          { level: 1, format: LevelFormat.BULLET, text: '\u25E6', alignment: AlignmentType.LEFT,
            style: { paragraph: { indent: { left: 840, hanging: 240 } } } }
        ]
      },
      {
        reference: 'numbers',
        levels: [
          { level: 0, format: LevelFormat.DECIMAL, text: '%1.', alignment: AlignmentType.LEFT,
            style: { paragraph: { indent: { left: 480, hanging: 240 } } } }
        ]
      }
    ]
  },
  styles: {
    default: {
      document: {
        run: { font: 'Arial', size: 22, color: COLORS.dark }
      }
    },
    paragraphStyles: [
      {
        id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 36, bold: true, font: 'Arial', color: COLORS.primary },
        paragraph: { spacing: { before: 360, after: 180 }, outlineLevel: 0 }
      },
      {
        id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 28, bold: true, font: 'Arial', color: COLORS.secondary },
        paragraph: { spacing: { before: 280, after: 140 }, outlineLevel: 1 }
      },
      {
        id: 'Heading3', name: 'Heading 3', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 24, bold: true, font: 'Arial', color: COLORS.dark },
        paragraph: { spacing: { before: 200, after: 100 }, outlineLevel: 2 }
      },
      {
        id: 'Heading4', name: 'Heading 4', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 22, bold: true, font: 'Arial', color: COLORS.gray },
        paragraph: { spacing: { before: 160, after: 80 }, outlineLevel: 3 }
      }
    ]
  },
  sections: [{
    properties: {
      page: {
        size: { width: PAGE_WIDTH, height: PAGE_HEIGHT },
        margin: { top: MARGIN, right: MARGIN, bottom: MARGIN, left: MARGIN }
      }
    },
    headers: {
      default: new Header({
        children: [new Paragraph({
          alignment: AlignmentType.RIGHT,
          border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: COLORS.secondary, space: 1 } },
          spacing: { before: 0, after: 120 },
          children: [new TextRun({ text: 'DynamicIsland for Windows \u2014 源码阅读文档', font: 'Arial', size: 18, color: COLORS.gray })]
        })]
      })
    },
    footers: {
      default: new Footer({
        children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          border: { top: { style: BorderStyle.SINGLE, size: 6, color: COLORS.secondary, space: 1 } },
          spacing: { before: 120, after: 0 },
          children: [
            new TextRun({ text: '第 ', font: 'Arial', size: 18, color: COLORS.gray }),
            new TextRun({ children: [PageNumber.CURRENT], font: 'Arial', size: 18, color: COLORS.gray }),
            new TextRun({ text: ' 页 / 共 ', font: 'Arial', size: 18, color: COLORS.gray }),
            new TextRun({ children: [PageNumber.TOTAL_PAGES], font: 'Arial', size: 18, color: COLORS.gray }),
            new TextRun({ text: ' 页', font: 'Arial', size: 18, color: COLORS.gray })
          ]
        })]
      })
    },
    children: [

      // ===== 封面 =====
      new Paragraph({ spacing: { before: 2000, after: 0 }, children: [] }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 200),
        children: [new TextRun({
          text: 'DynamicIsland for Windows',
          font: 'Arial',
          size: 56,
          bold: true,
          color: COLORS.primary
        })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 400),
        children: [new TextRun({
          text: '项目源码阅读文档',
          font: 'Arial',
          size: 40,
          color: COLORS.secondary
        })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 100),
        children: [new TextRun({
          text: 'Source Code Reading Guide',
          font: 'Arial',
          size: 28,
          color: COLORS.gray,
          italics: true
        })]
      }),
      new Paragraph({ spacing: { before: 1200, after: 0 }, children: [] }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 80),
        children: [new TextRun({ text: '文档版本: 1.0', font: 'Arial', size: 22, color: COLORS.gray })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 80),
        children: [new TextRun({ text: '生成时间: 2026-04-03', font: 'Arial', size: 22, color: COLORS.gray })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 80),
        children: [new TextRun({ text: '技术栈: C++17 / Win32 / Direct2D / WinRT / WASAPI', font: 'Arial', size: 22, color: COLORS.gray })]
      }),

      // ===== 目录 =====
      new Paragraph({ children: [new PageBreak()] }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 300),
        children: [tb('目 录', { size: 36, color: COLORS.primary })]
      }),
      new TableOfContents('目录', { hyperlink: true, headingStyleRange: '1-4' }),

      // ===== 第1章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '1. 项目概述'),
      p('DynamicIsland for Windows 是一个 C++/Win32 桌面应用程序，旨在复刻 iPhone 14 Pro 的"灵动岛"交互体验。它是一个悬浮在屏幕顶部的药丸形 UI，能够智能检测和展示系统任务状态（音乐播放、番茄钟、通知等），并提供丰富的视觉反馈和动画效果。'),
      p(''),
      heading(2, '1.1 核心特性'),
      simpleTable(
        ['特性', '描述'],
        [
          ['音乐可视化', '监控 Spotify/网易云音乐播放，显示专辑封面、进度条、歌词'],
          ['通知提醒', '捕获第三方应用 Toast 通知，以 Alert 形式展示'],
          ['网络状态', '监控 WiFi/蓝牙连接状态变化，自动弹出提醒'],
          ['文件拖放', '支持将文件拖放到悬浮球上暂存'],
          ['番茄钟', '内置计时器，专注/休息交替'],
          ['天气插件', '显示当前天气和温度'],
          ['系统监控', '电量、充电状态监控'],
          ['多Island模式', '支持 Main/Mini/Pomodoro/Stats 四种 Island 类型'],
          ['设置面板', '提供丰富的自定义配置选项'],
        ],
        [2800, 6560]
      ),

      // ===== 第2章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '2. 技术栈与依赖'),
      heading(2, '2.1 技术层次'),
      simpleTable(
        ['层次', '技术'],
        [
          ['语言', 'C++17'],
          ['GUI 框架', 'Win32 API (原生窗口)'],
          ['图形渲染', 'Direct2D + DirectComposition'],
          ['音频会话', 'Windows Core Audio API (IMMDeviceEnumerator, IAudioSessionManager2)'],
          ['媒体控制', 'Windows.Media.Control (GlobalSystemMediaTransportControlsSessionManager)'],
          ['通知捕获', 'WinRT (Windows.UI.Notifications)'],
          ['HTTP 通信', 'WinHTTP'],
          ['插件接口', 'COM 接口模式'],
          ['构建系统', 'MSBuild / Visual Studio 2022'],
          ['目标平台', 'Windows 10/11 x64'],
        ],
        [2800, 6560]
      ),
      p(''),
      heading(2, '2.2 关键头文件依赖'),
      codeTable(
`// WinRT 相关
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.UI.Notifications.h>

// Direct2D 相关
#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <dcomp.h>

// WASAPI 音频
#include <Audioclient.h>
#include <mmdeviceapi.h>`,

      ),

      // ===== 第3章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '3. 目录结构'),
      p('项目根目录位于 E:\\vs\\c++\\DynamicIsland\\，主要包含以下目录和文件：'),
      p(''),
      heading(2, '3.1 核心源文件'),
      simpleTable(
        ['文件', '描述'],
        [
          ['main.cpp', '程序入口点 (wWinMain)'],
          ['DynamicIsland.cpp/h', '主窗口类实现 (~2000行)'],
          ['RenderEngine.cpp/h', 'Direct2D 渲染引擎 (~1400行)'],
          ['IslandState.h', '状态枚举和渲染上下文'],
          ['Messages.h', '窗口消息和数据结构定义'],
          ['Constants.h', '全局常量定义'],
          ['EventBus.h', '事件总线 (发布/订阅)'],
          ['ThreadSafeQueue.h', '线程安全队列模板'],
          ['Spring.h', '弹簧物理动画引擎'],
        ],
        [3200, 6160]
      ),
      p(''),
      heading(2, '3.2 监控模块'),
      simpleTable(
        ['文件', '描述'],
        [
          ['MediaMonitor.cpp/h', '媒体会话监控 (Spotify/网易云)'],
          ['LyricsMonitor.cpp/h', '歌词获取与解析 (~600行)'],
          ['NotificationMonitor.cpp/h', 'Windows 通知监控'],
          ['ConnectionMonitor.cpp/h', 'WiFi/蓝牙状态监控'],
          ['SystemMonitor.cpp/h', '电量/天气监控'],
          ['TaskDetector.cpp/h', '统一任务检测 (单例模式)'],
        ],
        [3200, 6160]
      ),
      p(''),
      heading(2, '3.3 UI 组件 (Components/)'),
      simpleTable(
        ['文件', '描述'],
        [
          ['MusicPlayerComponent.cpp/h', '音乐播放器 UI (专辑封面/进度条/歌词)'],
          ['AlertComponent.cpp/h', '警告提醒 UI (WiFi/蓝牙/通知)'],
          ['VolumeComponent.cpp/h', '音量显示 UI'],
        ],
        [3200, 6160]
      ),
      p(''),
      heading(2, '3.4 功能模块'),
      simpleTable(
        ['文件', '描述'],
        [
          ['WeatherPlugin.cpp/h', '天气插件 (~350行)'],
          ['PomodoroTimer.h', '番茄钟计时器'],
          ['SpotifyClient.h', 'Spotify Web API 客户端'],
          ['VolumeOSDController.h', '自定义音量 OSD'],
          ['NetworkDiscovery.h', '局域网设备发现 (UDP广播)'],
          ['LanTransferManager.h', '局域网文件传输'],
        ],
        [3200, 6160]
      ),
      p(''),
      heading(2, '3.5 系统组件'),
      simpleTable(
        ['文件', '描述'],
        [
          ['WindowManager.cpp/h', 'Win32 窗口封装'],
          ['IslandManager.h', '多 Island 管理'],
          ['SettingsWindow.cpp/h', '设置窗口 (~750行)'],
          ['FilePanelWindow.cpp/h', '文件暂存面板 (~350行)'],
          ['DropManager.h', 'OLE 拖放处理 (IDropTarget)'],
          ['PluginManager.h', '插件管理器 (IPlugin/IWeatherPlugin/IClockPlugin)'],
          ['IMessageHandler.h', '消息处理接口'],
        ],
        [3200, 6160]
      ),

      // ===== 第4章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '4. 核心架构解析'),
      heading(2, '4.1 入口点与初始化流程'),
      p('文件: main.cpp'),
      p('程序入口点负责初始化 WinRT 公寓、OLE（用于拖放功能）和 DPI 感知设置：'),
      p(''),
      codeTable(
`int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow)
{
    // 1. 初始化 WinRT 公寓
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // 2. 初始化 OLE (用于拖放功能)
    OleInitialize(nullptr);

    // 3. 设置 DPI 感知
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 4. 创建并运行 DynamicIsland 实例
    DynamicIsland app;
    app.Initialize();
    return app.Run();
}`),
      p(''),
      p('初始化序列:'),
      bullet('DynamicIsland::Initialize() \u2014 创建窗口、初始化渲染引擎、启动各监控线程'),
      bullet('DynamicIsland::Run() \u2014 进入消息循环'),

      heading(2, '4.2 窗口管理与消息循环'),
      p('文件: DynamicIsland.cpp/h (~2000行)'),
      p('DynamicIsland 类是应用的核心，主窗口的创建和消息处理都在这里：'),
      p(''),
      heading(3, '4.2.1 关键成员'),
      simpleTable(
        ['成员', '类型', '作用'],
        [
          ['m_hwnd', 'HWND', '主窗口句柄'],
          ['m_renderEngine', 'RenderEngine*', '渲染引擎指针'],
          ['m_mediaMonitor', 'MediaMonitor*', '媒体监控'],
          ['m_lyricsMonitor', 'LyricsMonitor*', '歌词监控'],
          ['m_notificationMonitor', 'NotificationMonitor*', '通知监控'],
          ['m_connectionMonitor', 'ConnectionMonitor*', '网络监控'],
          ['m_systemMonitor', 'SystemMonitor*', '系统监控'],
          ['m_widthSpring', 'Spring', '宽度弹簧动画'],
          ['m_heightSpring', 'Spring', '高度弹簧动画'],
          ['m_trayIcon', 'NOTIFYICONDATA', '托盘图标数据'],
        ],
        [2000, 2400, 4960]
      ),
      p(''),
      heading(3, '4.2.2 关键消息处理'),
      simpleTable(
        ['消息', '处理逻辑'],
        [
          ['WM_CREATE', '创建窗口、初始化渲染引擎'],
          ['WM_DESTROY', '清理资源、退出消息循环'],
          ['WM_TIMER', '驱动动画更新 (~16ms 一次)'],
          ['WM_MOUSEMOVE', '鼠标悬停检测、更新状态'],
          ['WM_LBUTTONDOWN', '区分点击区域 (Expand/Button)，分发点击'],
          ['WM_LBUTTONUP', '触发按钮回调'],
          ['WM_HOTKEY', '处理全局热键 (显示/隐藏/设置)'],
          ['WM_DROP_FILE', '文件拖放处理，添加到 FilePanel'],
          ['WM_POWERBROADCAST', '电源事件 (睡眠/唤醒/电源状态变化)'],
          ['WM_DPICHANGED', 'DPI 变化通知'],
        ],
        [2400, 6960]
      ),

      heading(2, '4.3 渲染引擎'),
      p('文件: RenderEngine.cpp/h (~1400行)'),
      p('渲染引擎基于 Direct2D 和 DirectComposition，负责所有 UI 绘制。'),
      p(''),
      heading(3, '4.3.1 初始化流程'),
      codeTable(
`void RenderEngine::Initialize(HWND hwnd)
{
    // 1. 创建 D3D 设备 (D3D11CreateDevice)
    // 2. 创建 D2D 工厂 (D2D1CreateFactory)
    // 3. 创建 DirectComposition 设备
    // 4. 为主窗口创建 HwndTarget (DirectComposition)
    // 5. 创建主绘制表面
    // 6. 初始化字体、画笔等 GDI 资源
}`),
      p(''),
      heading(3, '4.3.2 渲染入口'),
      codeTable(
`void RenderEngine::DrawCapsule(
    ID2D1DeviceContext5* dc,      // D2D 设备上下文
    D2D1_POINT_2F topLeft,        // 胶囊左上角
    D2D1_POINT_2F bottomRight,   // 胶囊右下角
    float opacity,                // 透明度
    IslandDisplayMode mode,       // 显示模式
    bool isAlert,                 // 是否为警告状态
    const RenderContext& ctx      // 渲染上下文
)`),

      heading(2, '4.4 状态机与显示模式'),
      p('文件: IslandState.h'),
      p('IslandDisplayMode 枚举定义了 Island 的所有可能状态：'),
      p(''),
      codeTable(
`enum class IslandDisplayMode
{
    Idle,              // 空闲状态 (仅显示小圆点/短胶囊)
    MusicCompact,       // 音乐 - 紧凑视图 (胶囊)
    MusicExpanded,      // 音乐 - 展开视图 (大胶囊，显示更多详情)
    Alert,              // 警告提醒 (WiFi/蓝牙/通知等)
    Volume,             // 音量调节
    FileDrop,           // 文件拖放
};`),
      p(''),
      p('状态转换逻辑:'),
      bullet('Idle \u2192 MusicCompact: 检测到音乐播放时'),
      bullet('MusicCompact \u2192 MusicExpanded: 用户点击展开'),
      bullet('MusicExpanded \u2192 Idle: 音乐结束或超时'),
      bullet('Idle \u2192 Alert: 检测到 WiFi/蓝牙连接或通知'),
      bullet('Alert \u2192 Idle: 警告消失或超时'),
      bullet('Idle \u2192 Volume: 系统音量变化时'),

      heading(2, '4.5 弹簧物理动画系统'),
      p('文件: Spring.h'),
      p('使用弹簧物理模型实现平滑的尺寸过渡动画：'),
      p(''),
      codeTable(
`class Spring
{
public:
    Spring(float tension = 180.f, float damping = 12.f)
        : m_tension(tension), m_damping(damping) {}

    bool Update(float dt);    // 更新动画，返回是否仍在运动
    void SetTarget(float target) { m_target = target; }
    float GetValue() const { return m_value; }

private:
    float m_value{0};        // 当前值
    float m_target{0};      // 目标值
    float m_velocity{0};     // 当前速度
    float m_tension{180.f};  // 弹簧张力
    float m_damping{12.f};   // 阻尼系数
};`),
      p(''),
      p('物理模型:'),
      bullet('受力 = 张力 \u00d7 (目标值 - 当前值) - 阻尼 \u00d7 速度'),
      bullet('速度 += 受力 \u00d7 dt'),
      bullet('位置 += 速度 \u00d7 dt'),
      p(''),
      p('两个独立的 Spring 实例分别控制 Island 的宽度和高度，在 WM_TIMER 消息中 (~16ms 一次) 更新。'),

      // ===== 第5章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '5. 核心模块详解'),
      heading(2, '5.1 DynamicIsland 主类'),
      p('文件: DynamicIsland.cpp/h (~2000行)'),
      p('DynamicIsland 是整个应用的核心类，协调所有子系统。它实现 IMessageHandler 接口，处理 Win32 窗口消息。'),
      p(''),
      heading(3, '5.1.1 核心方法'),
      simpleTable(
        ['方法', '说明'],
        [
          ['Initialize()', '初始化所有组件、创建窗口、注册热键'],
          ['Run()', '进入消息循环'],
          ['WndProc()', '窗口消息处理中心'],
          ['UpdateWindowRegion()', '根据当前尺寸更新窗口区域 (圆角矩形)'],
          ['StartAnimationTimer()', '启动 ~16ms 定时器驱动动画'],
          ['HandleMouseClick()', '处理鼠标点击，区分 Expand 区域和按钮'],
          ['ShowSettings()', '显示设置窗口'],
          ['ShowTrayMenu()', '显示托盘菜单'],
        ],
        [3200, 6160]
      ),

      heading(2, '5.2 MediaMonitor 媒体监控'),
      p('文件: MediaMonitor.cpp/h'),
      p('使用 Windows.Media.Control (WinRT) 和 WASAPI 两种方案监控媒体播放。'),
      p(''),
      heading(3, '5.2.1 架构'),
      codeTable(
`class MediaMonitor
{
    // WinRT 方案 - GlobalSystemMediaTransportControlsSessionManager
    winrt::GlobalSystemMediaTransportControlsSessionManager m_sessionManager;

    // 背景轮询线程
    std::thread m_pollingThread;
    std::atomic<bool> m_running{true};

    // 音频会话监控 (备用)
    wil::com_ptr<IMMDeviceEnumerator> m_deviceEnumerator;
    wil::com_ptr<IAudioSessionManager2> m_audioSessionManager;
};`),
      p(''),
      heading(3, '5.2.2 监控流程'),
      numbered('初始化 WinRT Session Manager: GlobalSystemMediaTransportControlsSessionManager::GetAsync()'),
      numbered('注册媒体状态回调: sessionManager.add_SessionChanged()'),
      numbered('后台线程轮询 (~500ms): 检查当前播放曲目、获取专辑封面、获取播放进度'),

      heading(2, '5.3 LyricsMonitor 歌词监控'),
      p('文件: LyricsMonitor.cpp/h (~600行)'),
      p('从网易云音乐 API 获取歌词，并解析 LRC 格式。'),
      p(''),
      heading(3, '5.3.1 LRC 格式解析'),
      codeTable(
`// LRC 格式示例:
[00:12.34]这是一行歌词
// 时间戳格式: [mm:ss.xx]

struct LyricLine {
    int minutes;
    int seconds;
    int milliseconds;
    std::wstring text;
};`),
      p(''),
      heading(3, '5.3.2 缓存机制'),
      p('歌词文件缓存到 x64/Release/lyrics_cache/{md5(song+artist)}.lrc，首次获取后缓存到本地。'),

      heading(2, '5.4 NotificationMonitor 通知监控'),
      p('文件: NotificationMonitor.cpp/h'),
      p('使用 WinRT ToastNotification 监控第三方应用通知：'),
      p(''),
      codeTable(
`using namespace winrt::Windows::UI::Notifications;

// 创建 ToastNotificationManager 并注册监听
m_toastNotificationManager = ToastNotificationManager::CreateToastNotifier(...);

// 监听通知到达
m_toastNotificationManager.add_ToastNotificationArrived(
    [this](auto&&, ToastNotificationEventArgs e) {
        // 解析通知内容，通过 EventBus 发送到 DynamicIsland
    }
);`),

      heading(2, '5.5 ConnectionMonitor 网络监控'),
      p('文件: ConnectionMonitor.cpp/h'),
      p('监控 WiFi 和蓝牙连接状态变化：'),
      bullet('无线网络状态: WlanGetAvailableNetworkList'),
      bullet('蓝牙状态: BluetoothFindFirstRadio / DeviceInquiry'),
      p('状态变化时通过 EventBus::PublishAlert() 发布警告事件。'),

      heading(2, '5.6 SystemMonitor 系统监控'),
      p('文件: SystemMonitor.cpp/h'),
      p('处理 WM_POWERBROADCAST 消息监控电源状态：'),
      bullet('PBT_APMPOWERSTATUSCHANGE \u2014 电量变化'),
      bullet('PBT_APMRESUMESUSPEND \u2014 从睡眠唤醒'),
      p('同时管理 WeatherPlugin 实例。'),

      heading(2, '5.7 WeatherPlugin 天气插件'),
      p('文件: WeatherPlugin.cpp/h (~350行)'),
      p('实现 IWeatherPlugin COM 接口，使用 WinHTTP 发送 HTTP 请求获取天气数据。'),
      p(''),
      codeTable(
`class WeatherPlugin : public IWeatherPlugin
{
public:
    void FetchWeather(const std::wstring& city);
    void SetApiKey(const std::wstring& key);

    int GetTemperature() const { return m_temperature; }
    const wchar_t* GetDescription() const { return m_description.c_str(); }
    const wchar_t* GetCity() const { return m_city.c_str(); }
};`),

      heading(2, '5.8 TaskDetector 任务检测器'),
      p('文件: TaskDetector.cpp/h'),
      p('单例模式，统一管理所有任务检测，协调各个 Monitor，通过 EventBus 接收事件，决定显示哪个 Island。'),
      p(''),
      heading(3, '5.8.1 关键方法'),
      simpleTable(
        ['方法', '说明'],
        [
          ['HasActiveMusic()', '是否有活跃音乐'],
          ['HasActiveLyrics()', '是否有活跃歌词'],
          ['HasActiveAlert()', '是否有活跃警告'],
          ['OnMediaUpdate()', '媒体更新回调'],
          ['OnAlert()', '警告回调'],
        ],
        [3200, 6160]
      ),

      heading(2, '5.9 SettingsWindow 设置窗口'),
      p('文件: SettingsWindow.cpp/h (~750行)'),
      p('分类设置的基于对话框的 UI：'),
      simpleTable(
        ['分类', '设置项'],
        [
          ['General', '开机启动、热键配置'],
          ['Appearance', '主题颜色、透明度'],
          ['MainUI', '尺寸、动画参数'],
          ['FilePanel', '文件面板设置'],
          ['About', '版本信息'],
        ],
        [3200, 6160]
      ),

      heading(2, '5.10 FilePanelWindow 文件面板窗口'),
      p('文件: FilePanelWindow.cpp/h (~350行)'),
      p('当用户拖放文件到 Island 上时，显示一个临时文件列表窗口。'),

      // ===== 第6章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '6. UI 组件系统'),
      heading(2, '6.1 MusicPlayerComponent 音乐播放器组件'),
      p('文件: Components/MusicPlayerComponent.cpp/h'),
      p(''),
      heading(3, '6.1.1 Compact 模式'),
      p('紧凑视图显示当前播放的歌曲信息：'),
      codeTable(
`┌────────────────────────────┐
│ 🎵 Song Title - Artist    │
└────────────────────────────┘
   进度条 (渐变填充)`),
      p(''),
      heading(3, '6.1.2 Expanded 模式'),
      codeTable(
`┌──────────────────────────────────────────┐
│ ┌──────┐  Song Title            ◀ ▶ ▐▐  │
│ │ 封面  │  Artist Name                   │
│ │ 48x48│                                 │
│ └──────┘  ████████░░░░░░░░░░  00:42/03:21│
│           当前歌词行...                   │
└──────────────────────────────────────────┘`),

      heading(2, '6.2 AlertComponent 警告组件'),
      p('文件: Components/AlertComponent.cpp/h'),
      p('显示 WiFi/蓝牙/通知等系统状态变化：'),
      simpleTable(
        ['类型', '描述'],
        [
          ['AlertType::WiFi', 'WiFi 连接'],
          ['AlertType::Bluetooth', '蓝牙连接'],
          ['AlertType::Notification', '通知'],
          ['AlertType::BatteryLow', '低电量'],
          ['AlertType::Charging', '充电中'],
        ],
        [3200, 6160]
      ),

      heading(2, '6.3 VolumeComponent 音量组件'),
      p('文件: Components/VolumeComponent.cpp/h'),
      p('显示系统音量调节条：'),
      codeTable(
`┌──────────────────────────────────┐
│ 🔊 ████████░░░░░░░░  75%         │
└──────────────────────────────────┘`),

      // ===== 第7章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '7. 事件通信机制'),
      heading(2, '7.1 EventBus 事件总线'),
      p('文件: EventBus.h'),
      p('单例模式的发布/订阅系统，用于各监控线程与主线程之间的安全通信：'),
      p(''),
      codeTable(
`class EventBus
{
public:
    static EventBus& Instance();

    // 订阅
    template<typename T>
    void Subscribe(std::function<void(const T&)> callback);

    // 发布
    template<typename T>
    void Publish(const T& event);
};`),
      p(''),
      heading(3, '7.1.1 事件类型'),
      codeTable(
`struct MediaUpdateEvent {
    std::wstring title;
    std::wstring artist;
    std::vector<uint8_t> albumArt;
    int durationMs;
    int positionMs;
    bool isPlaying;
};

struct AlertEvent {
    AlertType type;
    std::wstring title;
    std::wstring description;
    int durationMs;
};

struct VolumeChangeEvent {
    int volume;    // 0-100
    bool isMuted;
};`),

      heading(2, '7.2 ThreadSafeQueue 线程安全队列'),
      p('文件: ThreadSafeQueue.h'),
      p('模板实现的线程安全队列，基于互斥锁和条件变量：'),
      p(''),
      codeTable(
`template<typename T>
class ThreadSafeQueue
{
public:
    void Enqueue(T item);
    bool TryDequeue(T& item);
    size_t Size() const;
    bool IsEmpty() const;

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};`),

      // ===== 第8章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '8. 插件系统'),
      p('文件: PluginManager.h'),
      p('定义插件接口，支持扩展天气、时钟等功能：'),
      p(''),
      codeTable(
`// 天气插件接口
class IWeatherPlugin : public IUnknown
{
public:
    virtual void FetchWeather(const std::wstring& city) = 0;
    virtual int GetTemperature() const = 0;
    virtual const wchar_t* GetDescription() const = 0;
};

// 时钟插件接口
class IClockPlugin : public IUnknown
{
public:
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual int GetRemainingSeconds() const = 0;
};

// 插件管理器
class PluginManager
{
    std::vector<wil::com_ptr<IUnknown>> m_plugins;
public:
    template<typename T>
    wil::com_ptr<T> GetPlugin();
};`),

      // ===== 第9章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '9. 拖放文件处理'),
      p('文件: DropManager.h'),
      p('实现 IDropTarget COM 接口，支持 OLE 拖放：'),
      p(''),
      codeTable(
`class DropManager : public IDropTarget
{
public:
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState,
                           POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt,
                          DWORD* pdwEffect) override;
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState,
                      POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragLeave() override;
};`),
      p(''),
      p('在 WM_DROP_FILE 消息中处理：'),
      codeTable(
`case WM_DROP_FILE:
{
    WCHAR filePath[MAX_PATH];
    auto count = DragQueryFile((HDROP)wParam, 0xFFFFFFFF, nullptr, 0);
    for (uint32_t i = 0; i < count; i++) {
        DragQueryFile((HDROP)wParam, i, filePath, MAX_PATH);
        m_filePanelWindow->AddFile(filePath);
    }
    DragFinish((HDROP)wParam);
}`),

      // ===== 第10章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '10. 番茄钟功能'),
      p('文件: PomodoroTimer.h'),
      p('内置番茄钟计时器，支持工作/休息交替：'),
      p(''),
      heading(3, '10.1 关键方法'),
      simpleTable(
        ['方法', '说明'],
        [
          ['StartWork(minutes)', '开始工作阶段 (默认25分钟)'],
          ['StartBreak(minutes)', '开始休息阶段 (默认5分钟)'],
          ['Stop()', '停止计时器'],
          ['Pause() / Resume()', '暂停/继续'],
          ['GetRemainingSeconds()', '获取剩余秒数'],
          ['IsWorkPhase()', '是否工作阶段'],
        ],
        [3200, 6160]
      ),
      p(''),
      heading(3, '10.2 回调函数'),
      bullet('onWorkComplete \u2014 工作完成回调'),
      bullet('onBreakComplete \u2014 休息完成回调'),
      bullet('onTick(seconds) \u2014 每秒回调'),

      // ===== 第11章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '11. 局域网发现与传输'),
      heading(2, '11.1 NetworkDiscovery 网络发现'),
      p('文件: NetworkDiscovery.h'),
      p('使用 UDP 广播发现局域网内的其他设备：'),
      p(''),
      codeTable(
`class NetworkDiscovery
{
    // 发送 UDP 广播
    void StartDiscovery();
    // 监听响应
    void ListenForResponses();

    std::vector<DiscoveredDevice> m_devices;
};`),

      heading(2, '11.2 LanTransferManager 文件传输'),
      p('文件: LanTransferManager.h'),
      codeTable(
`class LanTransferManager
{
public:
    void UploadFile(const std::wstring& filePath,
                    const std::wstring& targetIP);
    void DownloadFile(const std::wstring& remoteFile,
                      const std::wstring& localPath);
};`),

      // ===== 第12章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '12. 配置系统'),
      p('文件: Constants.h'),
      p('所有可配置常量集中在 Constants 命名空间中，配置文件 config.ini (与 exe 同目录) 可覆盖默认值。'),
      p(''),
      heading(2, '12.1 常量分类'),
      simpleTable(
        ['命名空间', '内容'],
        [
          ['Constants::Size', 'MinWidth, MaxWidth, DefaultWidth, ExpandedWidth 等尺寸参数'],
          ['Constants::UI', 'CornerRadius, Opacity 等 UI 参数'],
          ['Constants::Animation', 'Tension, Damping 等动画参数'],
          ['Constants::Color', 'Background, TextPrimary 等颜色值'],
          ['Constants::Alert', 'Alert 显示时间等'],
          ['Constants::System', '系统相关参数'],
        ],
        [3200, 6160]
      ),
      p(''),
      heading(2, '12.2 配置文件格式'),
      codeTable(
`[General]
Width=140
Opacity=0.9
CornerRadius=55

[Animation]
Tension=200
Damping=15`),

      // ===== 第13章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '13. 构建系统'),
      p('文件: DynamicIsland.vcxproj'),
      p('使用 MSBuild / Visual Studio 2022 构建：'),
      p(''),
      heading(3, '13.1 关键配置'),
      codeTable(
`<PlatformToolset>v143</PlatformToolset>           <!-- VS 2022 -->
<WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
<LanguageStandard>stdcpp17</LanguageStandard>
<ConsumeWinRT>true</ConsumeWinRT>`),
      p(''),
      heading(3, '13.2 关键依赖库'),
      codeTable(
`d3d11.lib;      <!-- Direct3D 11 -->
d2d1.lib;       <!-- Direct2D -->
dxgi.lib;       <!-- DXGI -->
dcomp.lib;      <!-- DirectComposition -->
winmm.lib;      <!-- Windows Multimedia -->
imm32.lib;      <!-- Input Method Manager -->
audioclient.lib; <!-- WASAPI -->
ole32.lib;      <!-- OLE -->
oleaut32.lib;   <!-- OLE Automation -->
urlmon.lib;     <!-- HTTP -->
zlib.lib;       <!-- 压缩 -->`),

      // ===== 第14章 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '14. 关键数据结构和消息'),
      heading(2, '14.1 窗口消息定义'),
      p('文件: Messages.h'),
      p('自定义消息范围从 WM_USER + 1000 开始：'),
      p(''),
      codeTable(
`#define WM_USER_MIN              (WM_USER + 1000)
#define WM_MediaUpdate           (WM_USER_MIN + 1)   // 媒体更新
#define WM_Alert                 (WM_USER_MIN + 2)   // 警告事件
#define WM_VolumeChange          (WM_USER_MIN + 3)   // 音量变化
#define WM_FileDropped           (WM_USER_MIN + 4)   // 文件拖放
#define WM_TaskDetected          (WM_USER_MIN + 5)   // 任务检测
#define WM_SettingsChanged       (WM_USER_MIN + 6)   // 设置变更
#define WM_User                  (WM_USER_MIN + 7)   // 用户消息`),
      p(''),
      heading(2, '14.2 RenderContext 渲染上下文'),
      p('文件: IslandState.h'),
      codeTable(
`struct RenderContext
{
    // 音乐数据
    std::wstring songTitle;
    std::wstring artist;
    wil::com_ptr<ID2D1Bitmap> albumArt;
    int durationMs;
    int positionMs;
    bool isPlaying;

    // 歌词
    std::wstring currentLyric;
    std::wstring nextLyric;

    // 警告
    int alertType;
    std::wstring alertTitle;
    std::wstring alertDescription;

    // 音量
    int volume;       // 0-100
    bool isMuted;
    bool showVolumeBar;

    // 天气
    int temperature;
    std::wstring weatherDescription;
};`),

      heading(2, '14.3 IslandDisplayMode 显示模式'),
      codeTable(
`enum class IslandDisplayMode
{
    Idle = 0,
    MusicCompact = 1,
    MusicExpanded = 2,
    Alert = 3,
    Volume = 4,
    FileDrop = 5,
};`),

      // ===== 附录 =====
      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '附录 A: 代码行数统计'),
      simpleTable(
        ['模块', '约行数', '复杂度'],
        [
          ['DynamicIsland.cpp', '~2000', '高 (消息中枢)'],
          ['RenderEngine.cpp', '~1400', '高 (D2D 绘图)'],
          ['SettingsWindow.cpp', '~750', '中'],
          ['LyricsMonitor.cpp', '~600', '中'],
          ['WeatherPlugin.cpp', '~350', '低'],
          ['FilePanelWindow.cpp', '~350', '低'],
          ['Components/', '~800', '中'],
          ['总计', '~6000+', '\u2014'],
        ],
        [3200, 1600, 4560]
      ),

      new Paragraph({ children: [new PageBreak()] }),
      heading(1, '附录 B: 依赖关系图'),
      p('主要模块之间的依赖关系如下：'),
      p(''),
      codeTable(
`main.cpp
  └─> DynamicIsland (主窗口类)
        ├─> RenderEngine (Direct2D 渲染)
        │     └─> Components/ (MusicPlayer/Alert/Volume)
        │
        ├─> EventBus (发布/订阅)
        │     └─> TaskDetector (单例)
        │           ├─> MediaMonitor
        │           ├─> NotificationMonitor
        │           ├─> ConnectionMonitor
        │           ├─> WeatherPlugin
        │           └─> PomodoroTimer
        │
        ├─> IslandManager (多Island)
        ├─> SettingsWindow
        └─> FilePanelWindow`),

      new Paragraph({ spacing: { before: 600, after: 200 }, children: [] }),
      p(''),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: spacing(0, 0),
        children: [new TextRun({
          text: '\u2014 \u2014 \u2014 文档结束 \u2014 \u2014 \u2014',
          font: 'Arial',
          size: 22,
          color: COLORS.gray,
          italics: true
        })]
      }),
    ]
  }]
});

// 生成文档
const outputPath = path.join(__dirname, 'DynamicIsland源码阅读文档.docx');
Packer.toBuffer(doc).then(buffer => {
  fs.writeFileSync(outputPath, buffer);
  console.log('Word document generated: ' + outputPath);
}).catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
