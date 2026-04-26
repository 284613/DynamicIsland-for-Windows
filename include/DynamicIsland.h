// DynamicIsland.h
#pragma once
#include "ConnectionMonitor.h"
#include <queue>
#include "WindowManager.h"
#include "RenderEngine.h"
#include "AgentSessionMonitor.h"
#include "ClaudeHookBridge.h"
#include "CodexHookBridge.h"
#include "FaceUnlockBridge.h"
#include "ClaudeSessionStore.h"
#include "CodexSessionStore.h"
#include "MediaMonitor.h"
#include "Messages.h"
#include <shellapi.h>
#include <array>
#include <vector>
#include "NotificationMonitor.h"
#include "LyricsMonitor.h"
#include "SystemMonitor.h"
#include "FilePanelWindow.h"
#include "FileStashStore.h"
#include "TodoStore.h"
#include "LayoutController.h"
#include "Spring.h"
#include "Constants.h"
#include "SettingsWindow.h"
#include <d2d1_1.h>
#include <imm.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "imm32.lib")

#define WM_TRAYICON (WM_USER + 101)

enum DirtyFlags : uint32_t {
    Dirty_None           = 0,
    Dirty_SpringAnim     = 1 << 0,   // 弹簧动画未稳定
    Dirty_AudioLevel     = 1 << 1,   // 音频可视化活跃
    Dirty_TextScroll     = 1 << 2,   // 标题/歌词滚动中
    Dirty_MediaState     = 1 << 3,   // 播放/暂停/曲目变化
    Dirty_Progress       = 1 << 4,   // 播放进度变化
    Dirty_Lyrics         = 1 << 5,   // 歌词行变化
    Dirty_Volume         = 1 << 6,   // 音量条激活
    Dirty_Alert          = 1 << 7,   // 通知显示中
    Dirty_Hover          = 1 << 8,   // 鼠标交互
    Dirty_FileDrop       = 1 << 9,   // 拖拽状态变化
    Dirty_Weather        = 1 << 10,  // 天气数据更新
    Dirty_Time           = 1 << 11,  // 时间字符串更新
    Dirty_Region         = 1 << 12,  // 窗口区域需更新
    Dirty_AgentSessions  = 1 << 13,  // 会话中心数据更新
    Dirty_FaceUnlock     = 1 << 14,
};

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
    // 按需渲染基础设施
    uint32_t m_dirtyFlags = 0;
    bool m_renderLoopActive = false;
    int m_idleFrameCount = 0;          // 连续空闲帧计数（防抖动）
    static constexpr int IDLE_COOLDOWN = 5;  // 连续5帧无变化才停止

    void Invalidate(uint32_t flags);         // 标脏并确保渲染循环运行
    void EnsureRenderLoopRunning();          // 按需启动定时器
    bool ShouldKeepRendering() const;        // 判断是否继续渲染

    // 缓存的媒体状态（从事件驱动更新，替代逐帧轮询）
    std::wstring m_cachedTitle;
    std::wstring m_cachedArtist;
    bool m_cachedIsPlaying = false;
    bool m_cachedHasSession = false;
    bool m_darkMode = true;
    bool m_followSystemTheme = true;
    float m_mainUITransparency = 1.0f;
    float m_filePanelTransparency = 0.9f;
    float m_springStiffness = 400.0f;
    float m_springDamping = 30.0f;
    int m_mediaPollIntervalMs = 1000;
    int m_fileStashMaxItems = 5;
    bool m_autoStart = false;
    bool m_todoInputActive = false;

    void StartAnimation();
    void UpdatePhysics();
    void LoadConfig(); // 【新增】加载配置文件的函数
    void TransitionTo(IslandDisplayMode mode);
    void SetTargetSize(float width, float height);
    void SetActiveExpandedMode(ActiveExpandedMode mode);
    bool IsExpandedMode(ActiveExpandedMode mode) const;
    bool ShouldUseShrunkMode() const;
    bool HitTestShrunkHandle(POINT pt) const;
    bool HitTestCompactShrinkHandle(POINT pt, IslandDisplayMode mode) const;
    void BeginShrinkTransition(bool shrinkToShrunk, IslandDisplayMode sourceMode);
    void UpdateShrinkTransition(ULONGLONG nowMs);
    IslandDisplayMode DetermineBaseDisplayMode() const;
    IslandDisplayMode DetermineDisplayMode();
    std::vector<IslandDisplayMode> CollectAvailableCompactModes() const;
    void ClearCompactOverride();
    static bool IsCompactSwitchableMode(IslandDisplayMode mode);
    void LoadCompactModeOrder(const std::wstring& rawValue);
    std::wstring SerializeCompactModeOrder() const;
    SecondaryContentKind DetermineSecondaryContent() const;
    D2D1_RECT_F GetSecondaryRectLogical() const;
    bool HandleFileSecondaryMouseDown(POINT pt);
    bool HandleFileSecondaryMouseMove(HWND hwnd, POINT pt, WPARAM keyState);
    bool HandleFileSecondaryMouseUp(POINT pt);
    void ResetFileSecondaryInteraction();
    void ShowFileStashLimitAlert();
    void RemoveFileStashIndex(int index);
    HWND FindWindowBelowPoint(POINT screenPt) const;
    bool ForwardMouseMessageToWindow(HWND target, UINT message, WPARAM wParam, POINT screenPt) const;
    bool ForwardMouseMessageToUnderlyingWindow(UINT message, WPARAM wParam, POINT screenPt) const;
    POINT LogicalFromPhysical(POINT physicalPt) const;
    bool GetSystemDarkMode() const;
    void ApplyRuntimeSettings();
    void UpdateAutoStart(bool enabled);
    void OpenPomodoroPanel();
    void SyncPomodoroMode();
    void HandlePomodoroFinished();
    void OpenTodoInputCompact();
    void CloseTodoInputCompact();
    void OpenTodoPanel();
    void CloseTodoPanel();
    void OpenAgentPanel();
    void CloseAgentPanel();
    void RefreshAgentSessionState(bool preserveSelection);
    void SelectAgentSession(AgentKind kind, const std::wstring& sessionId);
    void HandleClaudeHookEvent(const ClaudeHookEvent& event);
    void HandleCodexHookEvent(const CodexHookEvent& event);
    void HandleFaceUnlockEvent(const FaceUnlockEvent& event);
    void ShowFaceUnlockFeedback(FaceIdState state, const std::wstring& text);
    void ClearFaceUnlockFeedback();
    void RunClaudeHookAction(int actionId);
    bool HasWorkingClaudeSession() const;
    bool ShouldShowClaudeWorkingEdgeBadge(IslandDisplayMode mode) const;
    float GetCompactTargetWidth(IslandDisplayMode mode) const;
    float GetCompactTargetHeight(IslandDisplayMode mode) const;
    void PositionActiveImeWindow();
    bool IsTodoTextMode(IslandDisplayMode mode) const;
    bool ShouldKeepCompactOverride() const;

    struct ProgressBarLayout {
        float left;
        float right;
    };

    ProgressBarLayout GetProgressBarLayout() const;
    static float ClampProgress(float progress);

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
    FilePanelWindow m_filePanel;
    SettingsWindow m_settingsWindow;
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
    AgentSessionMonitor m_agentSessionMonitor;
    ClaudeHookBridge m_claudeHookBridge;
    CodexHookBridge m_codexHookBridge;
    FaceUnlockBridge m_faceUnlockBridge;
    ClaudeSessionStore m_claudeSessionStore;
    CodexSessionStore m_codexSessionStore;
    TodoStore m_todoStore;
    std::vector<std::wstring> m_allowedApps;    // 配置中允许通知的软件
    // ============================================
    IslandState m_state = IslandState::Collapsed;
    bool m_hasCompactOverride = false;
    IslandDisplayMode m_compactOverrideMode = IslandDisplayMode::Idle;
    ActiveExpandedMode m_activeExpandedMode = ActiveExpandedMode::None;
    bool m_isHovering = false;  // 鼠标是否悬停
    bool m_shrunkWakeActive = false;
    bool m_manualShrunk = false;
    bool m_shrinkAnimating = false;
    bool m_shrinkToShrunk = false;
    ULONGLONG m_shrinkAnimationStartMs = 0;
    float m_shrinkProgress = 0.0f;
    IslandDisplayMode m_shrinkSourceMode = IslandDisplayMode::Idle;
    HWND m_forwardMouseTarget = nullptr;
    bool m_forwardMouseDragActive = false;
    AgentSessionFilter m_agentFilter = AgentSessionFilter::Claude;
    AgentKind m_selectedAgentKind = AgentKind::Claude;
    AgentKind m_compactAgentKind = AgentKind::Claude;
    bool m_agentChooserOpen = false;
    std::wstring m_selectedAgentSessionId;
    std::vector<AgentSessionSummary> m_agentSessionSummaries;
    std::vector<AgentHistoryEntry> m_selectedAgentHistory;
    std::vector<IslandDisplayMode> m_compactModeOrder;
    MusicArtworkStyle m_compactArtworkStyle = MusicArtworkStyle::Vinyl;
    MusicArtworkStyle m_expandedArtworkStyle = MusicArtworkStyle::Square;
    bool m_faceUnlockFeedbackActive = false;
    FaceIdState m_faceUnlockState = FaceIdState::Hidden;
    std::wstring m_faceUnlockText;
    UINT_PTR m_faceUnlockTimerId = 6;

    // LayoutController handles size, alpha, springs, and hit testing
    LayoutController m_layoutController;

    // PR6: Display mode priority table for configurable scheduling
    DisplayModePriorityTable m_priorityTable;

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


    UINT m_currentDpi = 96;       // 当前屏幕的 DPI
    float m_dpiScale = 1.0f;      // 缩放比例 (100% = 1.0f, 150% = 1.5f)


    bool m_isVolumeControlActive = false;
    float m_currentVolume = 0.0f;         // 当前音量值
    UINT_PTR m_volumeTimerId = 4;         // 音量条自动隐藏定时器
    UINT_PTR m_fullscreenTimerId = 5;      // 全屏检测定时器


    bool m_isDragHovering = false; // 【新增】是否有文件拖拽悬停在岛屿上方
    FileStashStore m_fileStash;
    bool m_fileSecondaryExpanded = false;
    int m_fileSelectedIndex = -1;
    int m_fileHoveredIndex = -1;
    int m_filePressedIndex = -1;
    int m_fileLastClickIndex = -1;
    ULONGLONG m_fileLastClickTime = 0;
    POINT m_filePressPoint{};
    bool m_fileDragStarted = false;
    bool m_fileSelfDropDetected = false;

    bool m_isFullscreen = false; // 全屏检测标志

    // 天气信息
    std::wstring m_weatherLocationText = L"北京";
    std::wstring m_weatherDesc = L"Sunny";
    float m_weatherTemp = 25.0f;
    WeatherViewMode m_weatherViewMode = WeatherViewMode::Hourly;
    bool IsFullscreen(); // 全屏检测
};




