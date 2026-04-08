// DynamicIsland.h
#pragma once
#include "ConnectionMonitor.h"
#include <queue>
#include "WindowManager.h"
#include "RenderEngine.h"
#include "MediaMonitor.h"
#include "Messages.h"
#include <shellapi.h>
#include <vector>
#include "NotificationMonitor.h"
#include "LyricsMonitor.h"
#include "SystemMonitor.h"
#include "LayoutController.h"
#include "Spring.h"
#include "Constants.h"
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON (WM_USER + 101)

enum class IslandState {
    Collapsed,  // 现在是Mini尺寸
    Expanded,
    Alert
};

class DynamicIsland : public IMessageHandler {
public:
    DynamicIsland();
    ~DynamicIsland();
    void UpdateWindowRegion();
    bool Initialize(HINSTANCE hInstance);
    void Run();
    virtual LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void StartAnimation();
    void UpdatePhysics();
    void LoadConfig(); // 【新增】加载配置文件的函数
    void TransitionTo(IslandDisplayMode mode);
    void SetTargetSize(float width, float height);
    IslandDisplayMode DetermineDisplayMode() const;

    float GetCurrentWidth() const { return m_layoutController.GetCurrentWidth(); }
    float GetCurrentHeight() const { return m_layoutController.GetCurrentHeight(); }
    float GetTargetWidth() const { return m_layoutController.GetTargetWidth(); }
    float GetTargetHeight() const { return m_layoutController.GetTargetHeight(); }
    float GetCurrentAlpha() const { return m_layoutController.GetCurrentAlpha(); }
    bool IsAnimating() const { return m_layoutController.IsAnimating(); }

    bool m_dragging = false;
    POINT m_dragStart{};
    POINT m_windowStart{};

    // 时间显示
    std::wstring GetCurrentTimeString();   // 获取当前时间字符串

private:
    WindowManager m_window;
    RenderEngine m_renderer;
    MediaMonitor m_mediaMonitor;



    // 尺寸成员变量，从 Constants.h 初始化默认值，可被 config.ini 覆盖
    float CANVAS_WIDTH = Constants::Size::CANVAS_WIDTH;
    float CANVAS_HEIGHT = Constants::Size::CANVAS_HEIGHT;
    float COLLAPSED_WIDTH = Constants::Size::COLLAPSED_WIDTH;
    float COLLAPSED_HEIGHT = Constants::Size::COLLAPSED_HEIGHT;
    float COMPACT_WIDTH = Constants::Size::COMPACT_WIDTH;
    float COMPACT_HEIGHT = Constants::Size::COMPACT_HEIGHT;
    float EXPANDED_WIDTH = Constants::Size::EXPANDED_WIDTH;
    float EXPANDED_HEIGHT = Constants::Size::EXPANDED_HEIGHT;
    float MUSIC_EXPANDED_HEIGHT = Constants::Size::MUSIC_EXPANDED_HEIGHT;
    float ALERT_WIDTH = Constants::Size::ALERT_WIDTH;
    float ALERT_HEIGHT = Constants::Size::ALERT_HEIGHT;

    float m_smoothedAudio = 0.0f;
    std::wstring m_lastAlbumArt;

    bool m_isAlertActive = false;               // 是否正在显示弹窗
    AlertInfo m_currentAlert;                   // 当前显示的提示数据
    std::queue<AlertInfo> m_alertQueue;         // 排队队列
    UINT_PTR m_alertTimerId = 3;                // 弹窗持续时间定时器

    void ProcessNextAlert();
    void ProcessAlertWithPriority(const AlertInfo& alert); // 【OPT-03】处理高优先级警告                    // 处理下一个提示的函数
    ConnectionMonitor m_connectionMonitor;      // 连接监听器
    NotificationMonitor m_notificationMonitor;
    LyricsMonitor m_lyricsMonitor; // 歌词监听器
    SystemMonitor m_systemMonitor; // 【新增】系统状态（电量）监控器
    std::vector<std::wstring> m_allowedApps;    // 配置中允许通知的软件
    // ============================================
    IslandState m_state = IslandState::Collapsed;
    bool m_isHovering = false;  // 鼠标是否悬停

    // LayoutController handles size, alpha, springs, and hit testing
    LayoutController m_layoutController;

    const int HOTKEY_ID = 1001;
    UINT_PTR m_timerId = 1;

    UINT_PTR m_displayTimerId = 2;
    ULONGLONG m_lastUpdateTime = 0;

private:
    NOTIFYICONDATA m_nid;
    void CreateTrayIcon();
    void RemoveTrayIcon();

    int m_hoveredButtonIndex = -1;
    int m_pressedButtonIndex = -1;

    // 进度条相关
    bool m_isDraggingProgress = false;
    bool m_justReleasedProgress = false; // 是否刚松开进度条
    ULONGLONG m_progressReleaseTime = 0; // 松开进度条的时间
    float m_tempProgress = 0.0f; // 拖动时的临时进度
    int m_hoveredProgress = -1;
    int m_pressedProgress = -1;

    // 文件面板交互
    int m_hoveredFileIndex = -1;
    bool m_isFileDeleteHovered = false;


    UINT m_currentDpi = 96;       // 当前屏幕的 DPI
    float m_dpiScale = 1.0f;      // 缩放比例 (100% = 1.0f, 150% = 1.5f)


    bool m_isVolumeControlActive = false; // 是否正在显示音量条
    float m_currentVolume = 0.0f;         // 当前音量值
    UINT_PTR m_volumeTimerId = 4;         // 音量条自动隐藏定时器
    UINT_PTR m_fullscreenTimerId = 5;      // 全屏检测定时器


    bool m_isDragHovering = false; // 【新增】是否有文件拖拽悬停在岛屿上方
    //std::wstring m_storedFilePath; // 保存暂存的文件路径
    std::vector<std::wstring> m_storedFiles; // 保存暂存的多个文件路径

    bool m_isFullscreen = false; // 全屏检测标志

    // 天气信息
    std::wstring m_weatherDesc = L"Sunny";
    bool IsFullscreen(); // 全屏检测
    float m_weatherTemp = 25.0f;
};


