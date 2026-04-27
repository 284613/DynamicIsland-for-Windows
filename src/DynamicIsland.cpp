//DynamicIsland.cpp

#include "DynamicIsland.h"

#include <algorithm>
#include "EventBus.h"

#include <windowsx.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#include <chrono>

#include "RenderEngine.h"
#include "AutoStartManager.h"
#include "LyricsMonitor.h"
#include "ClaudeHookInstaller.h"
#include "CodexHookInstaller.h"

namespace {
std::wstring GetExeDirectory() {
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return {};
    }
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath);
}

std::wstring GetConfigPath() {
    const std::wstring exeDirectory = GetExeDirectory();
    return exeDirectory.empty() ? L"config.ini" : exeDirectory + L"\\config.ini";
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }

    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }

    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::wstring ReadUtf8IniValue(const std::wstring& path, const std::wstring& section, const std::wstring& key, const std::wstring& fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return fallback;
    }

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::wstring text = Utf8ToWide(bytes);
    if (text.empty()) {
        return fallback;
    }

    std::wistringstream stream(text);
    std::wstring line;
    std::wstring currentSection;
    const std::wstring targetSection = L"[" + section + L"]";
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            currentSection = line;
            continue;
        }
        if (currentSection == targetSection && line.size() > key.size() &&
            line.compare(0, key.size(), key) == 0 && line[key.size()] == L'=') {
            return line.substr(key.size() + 1);
        }
    }

    return fallback;
}

const std::array<IslandDisplayMode, 4> kCompactConfigModes = {
    IslandDisplayMode::MusicCompact,
    IslandDisplayMode::PomodoroCompact,
    IslandDisplayMode::TodoListCompact,
    IslandDisplayMode::AgentCompact,
};

const wchar_t* CompactModeConfigName(IslandDisplayMode mode) {
    switch (mode) {
    case IslandDisplayMode::MusicCompact:
        return L"Music";
    case IslandDisplayMode::PomodoroCompact:
        return L"Pomodoro";
    case IslandDisplayMode::TodoListCompact:
        return L"Todo";
    case IslandDisplayMode::AgentCompact:
        return L"Agent";
    default:
        return L"";
    }
}

bool EqualsNoCase(const std::wstring& lhs, const std::wstring& rhs) {
    return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring TrimToken(std::wstring value) {
    const size_t start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        return {};
    }
    const size_t end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

MusicArtworkStyle ParseArtworkStyle(const std::wstring& value, MusicArtworkStyle fallback) {
    const std::wstring token = TrimToken(value);
    if (EqualsNoCase(token, L"Square")) {
        return MusicArtworkStyle::Square;
    }
    if (EqualsNoCase(token, L"Vinyl")) {
        return MusicArtworkStyle::Vinyl;
    }
    return fallback;
}
}

#ifdef _DEBUG
std::wstring GetInputDebugLogPath() {
    const std::wstring exeDirectory = GetExeDirectory();
    return exeDirectory.empty() ? L"DynamicIsland_input_debug.log"
                                : exeDirectory + L"\\DynamicIsland_input_debug.log";
}

void AppendInputDebugLog(const std::wstring& message) {
    static std::mutex s_logMutex;
    std::lock_guard<std::mutex> lock(s_logMutex);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::wostringstream oss;
    oss << L"["
        << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"." << st.wMilliseconds
        << L"] " << message << L"\r\n";

    const std::wstring line = oss.str();
    OutputDebugStringW(line.c_str());
    std::wofstream out(GetInputDebugLogPath(), std::ios::app);
    if (out.is_open()) {
        out << line;
    }
}
#else
void AppendInputDebugLog(const std::wstring&) {}
#endif

bool PathsEqualIgnoreCase(const std::wstring& lhs, const std::wstring& rhs) {
    return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring ExtractIconFromExe(const std::wstring& appName) {

    std::wstring iconPath;

    wchar_t exePath[MAX_PATH];

    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return iconPath;

    // Extract only once, locate the current .exe directory

    PathRemoveFileSpecW(exePath);

    std::wstring base(exePath);

    // Match app name and point to icon path

    if (appName.find(L"QQ") != std::wstring::npos &&
        appName.find(L"\\u538b\\u4e0b") == std::wstring::npos) {
        iconPath = base + L"\\icon\\QQ.png";
    }
    else if (appName.find(L"\\u5fae\\u4fe1") != std::wstring::npos ||
        appName.find(L"WeChat") != std::wstring::npos) {
        iconPath = base + L"\\icon\\Wechat.png";
    }
    return iconPath;
}

// 全屏检测：检测前台窗口是否占满整个屏幕
bool DynamicIsland::IsFullscreen() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    // 忽略灵动岛自身
    if (hwnd == m_window.GetHWND()) return false;
    
    // 获取窗口所属的显示器信息
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(hMonitor, &mi)) return false;
    
    // 获取窗口矩形
    RECT rcWindow;
    if (!GetWindowRect(hwnd, &rcWindow)) return false;
    
    // 判断是否全屏：窗口占据整个显示器区域
    return (rcWindow.left <= mi.rcMonitor.left &&
            rcWindow.top <= mi.rcMonitor.top &&
            rcWindow.right >= mi.rcMonitor.right &&
            rcWindow.bottom >= mi.rcMonitor.bottom);
}

DynamicIsland::~DynamicIsland()

{
    m_faceUnlockBridge.Stop();
    m_claudeHookBridge.Stop();
    m_codexHookBridge.Stop();
    m_agentSessionMonitor.Shutdown();
}

DynamicIsland::DynamicIsland()

{

}

POINT DynamicIsland::LogicalFromPhysical(POINT physicalPt) const {
    return {
        (LONG)std::round(physicalPt.x / m_dpiScale),
        (LONG)std::round(physicalPt.y / m_dpiScale),
    };
}

DynamicIsland::ProgressBarLayout DynamicIsland::GetProgressBarLayout() const {
    const float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
    const float right = left + GetCurrentWidth();
    const float artLeft = left + 20.0f;
    const float textLeft = artLeft + 60.0f + 15.0f;
    return {
        textLeft - 80.0f,
        textLeft + (right - 20.0f - textLeft),
    };
}

float DynamicIsland::ClampProgress(float progress) {
    return progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
}

bool DynamicIsland::GetSystemDarkMode() const {
    HKEY hKey;
    DWORD val = 0;
    DWORD sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz);
        RegCloseKey(hKey);
        return val == 0;
    }
    return true;
}

void DynamicIsland::UpdateAutoStart(bool enabled) {
    AutoStart::SetEnabled(enabled);
}

void DynamicIsland::ApplyRuntimeSettings() {
    m_darkMode = m_followSystemTheme ? GetSystemDarkMode() : m_darkMode;
    m_layoutController.SetSpringParams(m_springStiffness, m_springDamping);
    m_fileStash.SetMaxItems(1);
    m_mediaMonitor.SetPollIntervalMs(m_mediaPollIntervalMs);
    m_renderer.SetTheme(m_darkMode, m_mainUITransparency, m_filePanelTransparency);
    m_renderer.SetMusicArtworkStyles(m_compactArtworkStyle, m_expandedArtworkStyle);
    UpdateAutoStart(m_autoStart);
}

void DynamicIsland::LoadCompactModeOrder(const std::wstring& rawValue) {
    m_compactModeOrder.clear();

    std::wstring remaining = rawValue;
    size_t start = 0;
    while (start <= remaining.size()) {
        size_t comma = remaining.find(L',', start);
        const std::wstring token = TrimToken(remaining.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start));
        if (!token.empty()) {
            for (IslandDisplayMode mode : kCompactConfigModes) {
                if (EqualsNoCase(token, CompactModeConfigName(mode)) &&
                    std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), mode) == m_compactModeOrder.end()) {
                    m_compactModeOrder.push_back(mode);
                    break;
                }
            }
        }

        if (comma == std::wstring::npos) {
            break;
        }
        start = comma + 1;
    }

    for (IslandDisplayMode mode : kCompactConfigModes) {
        if (std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), mode) == m_compactModeOrder.end()) {
            m_compactModeOrder.push_back(mode);
        }
    }
}

std::wstring DynamicIsland::SerializeCompactModeOrder() const {
    std::wstring value;
    bool first = true;
    for (IslandDisplayMode mode : m_compactModeOrder) {
        const wchar_t* name = CompactModeConfigName(mode);
        if (!name || !name[0]) {
            continue;
        }
        if (!first) {
            value += L",";
        }
        value += name;
        first = false;
    }
    return value;
}

void DynamicIsland::SetActiveExpandedMode(ActiveExpandedMode mode) {
    m_activeExpandedMode = mode;
    if (mode != ActiveExpandedMode::Pomodoro) {
        if (auto* pomodoro = m_renderer.GetPomodoroComponent()) {
            pomodoro->SetExpanded(false);
        }
    }
    m_mediaMonitor.SetExpandedState(mode == ActiveExpandedMode::Music);
}

bool DynamicIsland::IsExpandedMode(ActiveExpandedMode mode) const {
    return m_activeExpandedMode == mode;
}

bool DynamicIsland::ShouldUseShrunkMode() const {
    if (m_state != IslandState::Collapsed) return false;
    if (!m_manualShrunk) return false;
    if (m_isAlertActive || m_isDragHovering || m_todoInputActive || m_isVolumeControlActive || m_faceUnlockFeedbackActive) return false;
    if (m_activeExpandedMode != ActiveExpandedMode::None) return false;
    if (auto* pomodoro = m_renderer.GetPomodoroComponent(); pomodoro && pomodoro->HasActiveSession()) return false;
    return true;
}

bool DynamicIsland::HitTestShrunkHandle(POINT pt) const {
    const float handleWidth = (std::max)(Constants::Size::SHRUNK_WIDTH, 54.0f);
    const float left = (CANVAS_WIDTH - handleWidth) * 0.5f;
    const float right = left + handleWidth;
    const float top = Constants::UI::TOP_MARGIN;
    const float hitBottom = top + 14.0f;
    return static_cast<float>(pt.x) >= left &&
        static_cast<float>(pt.x) <= right &&
        static_cast<float>(pt.y) >= top &&
        static_cast<float>(pt.y) <= hitBottom;
}

bool DynamicIsland::HitTestCompactShrinkHandle(POINT pt, IslandDisplayMode mode) const {
    if (mode == IslandDisplayMode::Shrunk || m_state != IslandState::Collapsed) {
        return false;
    }
    if (!IsCompactSwitchableMode(mode)) {
        return false;
    }
    if (GetCurrentHeight() < Constants::Size::COMPACT_MIN_HEIGHT) {
        return false;
    }

    const float centerX = CANVAS_WIDTH * 0.5f;
    const float top = Constants::UI::TOP_MARGIN;
    const float x = static_cast<float>(pt.x);
    const float y = static_cast<float>(pt.y);
    return x >= centerX - 24.0f &&
        x <= centerX + 24.0f &&
        y >= top &&
        y <= top + 14.0f;
}

void DynamicIsland::BeginShrinkTransition(bool shrinkToShrunk, IslandDisplayMode sourceMode) {
    if (sourceMode == IslandDisplayMode::Shrunk) {
        sourceMode = m_shrinkSourceMode == IslandDisplayMode::Shrunk ? IslandDisplayMode::Idle : m_shrinkSourceMode;
    }
    if (!IsCompactSwitchableMode(sourceMode)) {
        sourceMode = IslandDisplayMode::Idle;
    }

    m_shrinkAnimating = true;
    m_shrinkToShrunk = shrinkToShrunk;
    m_shrinkSourceMode = sourceMode;
    m_shrinkAnimationStartMs = GetTickCount64();
    m_shrinkProgress = shrinkToShrunk ? 0.0f : 1.0f;
    Invalidate(Dirty_SpringAnim | Dirty_Region | Dirty_Time | Dirty_MediaState | Dirty_AgentSessions | Dirty_Weather);
}

void DynamicIsland::UpdateShrinkTransition(ULONGLONG nowMs) {
    if (!m_shrinkAnimating) {
        return;
    }

    constexpr ULONGLONG kShrinkAnimationDurationMs = 280;
    const ULONGLONG elapsed = nowMs >= m_shrinkAnimationStartMs ? nowMs - m_shrinkAnimationStartMs : 0;
    float t = static_cast<float>(elapsed) / static_cast<float>(kShrinkAnimationDurationMs);
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    const float eased = t * t * (3.0f - 2.0f * t);
    m_shrinkProgress = m_shrinkToShrunk ? eased : (1.0f - eased);

    if (t >= 1.0f) {
        m_shrinkAnimating = false;
        m_shrinkProgress = m_shrinkToShrunk ? 1.0f : 0.0f;
    }
}

void DynamicIsland::OpenPomodoroPanel() {
    auto* pomodoro = m_renderer.GetPomodoroComponent();
    if (!pomodoro) {
        return;
    }

    m_todoInputActive = false;
    SetActiveExpandedMode(ActiveExpandedMode::Pomodoro);
    pomodoro->SetExpanded(true);
    m_state = IslandState::Expanded;
    KillTimer(m_window.GetHWND(), m_displayTimerId);
    TransitionTo(IslandDisplayMode::PomodoroExpanded);
}

void DynamicIsland::SyncPomodoroMode() {
    auto* pomodoro = m_renderer.GetPomodoroComponent();
    if (!pomodoro) {
        return;
    }

    if (pomodoro->IsExpanded()) {
        SetActiveExpandedMode(ActiveExpandedMode::Pomodoro);
        m_state = IslandState::Expanded;
        KillTimer(m_window.GetHWND(), m_displayTimerId);
        TransitionTo(IslandDisplayMode::PomodoroExpanded);
        return;
    }

    if (IsExpandedMode(ActiveExpandedMode::Pomodoro)) {
        SetActiveExpandedMode(ActiveExpandedMode::None);
    }
    m_state = IslandState::Collapsed;
    TransitionTo(DetermineDisplayMode());
}

void DynamicIsland::HandlePomodoroFinished() {
    if (IsExpandedMode(ActiveExpandedMode::Pomodoro)) {
        SetActiveExpandedMode(ActiveExpandedMode::None);
    }
    m_state = IslandState::Collapsed;
    AlertInfo info{ 3, L"番茄时钟", L"本轮专注已完成", L"", {} };
    EventBus::GetInstance().PublishNotificationArrived(info);
    Invalidate(Dirty_Alert | Dirty_Region | Dirty_Time);
}

void DynamicIsland::OpenTodoInputCompact() {
    SetActiveExpandedMode(ActiveExpandedMode::None);
    m_todoInputActive = true;
    m_state = IslandState::Collapsed;
    KillTimer(m_window.GetHWND(), m_displayTimerId);
    const bool alreadyCompactSize =
        std::abs(GetTargetWidth() - Constants::Size::COMPACT_WIDTH) < 0.5f &&
        std::abs(GetTargetHeight() - Constants::Size::COMPACT_HEIGHT) < 0.5f &&
        std::abs(GetCurrentWidth() - Constants::Size::COMPACT_WIDTH) < 0.5f &&
        std::abs(GetCurrentHeight() - Constants::Size::COMPACT_HEIGHT) < 0.5f;
    if (!alreadyCompactSize) {
        TransitionTo(IslandDisplayMode::TodoInputCompact);
    } else {
        Invalidate(Dirty_Time | Dirty_Region | Dirty_Hover);
    }
    if (auto* todo = m_renderer.GetTodoComponent()) {
        todo->SetDisplayMode(IslandDisplayMode::TodoInputCompact);
        todo->BeginCompactInputFocus();
    }
    SetForegroundWindow(m_window.GetHWND());
    SetActiveWindow(m_window.GetHWND());
    SetFocus(m_window.GetHWND());
    PositionActiveImeWindow();
}

void DynamicIsland::CloseTodoInputCompact() {
    m_todoInputActive = false;
    m_state = IslandState::Collapsed;
    TransitionTo(DetermineDisplayMode());
}

void DynamicIsland::OpenTodoPanel() {
    m_todoInputActive = false;
    SetActiveExpandedMode(ActiveExpandedMode::Todo);
    m_state = IslandState::Expanded;
    KillTimer(m_window.GetHWND(), m_displayTimerId);
    TransitionTo(IslandDisplayMode::TodoExpanded);
    SetForegroundWindow(m_window.GetHWND());
    SetActiveWindow(m_window.GetHWND());
    SetFocus(m_window.GetHWND());
    PositionActiveImeWindow();
}

void DynamicIsland::CloseTodoPanel() {
    if (IsExpandedMode(ActiveExpandedMode::Todo)) {
        SetActiveExpandedMode(ActiveExpandedMode::None);
    }
    m_state = IslandState::Collapsed;
    TransitionTo(DetermineDisplayMode());
}

void DynamicIsland::OpenAgentPanel() {
    m_agentChooserOpen = false;
    m_agentFilter = (m_compactAgentKind == AgentKind::Codex) ? AgentSessionFilter::Codex : AgentSessionFilter::Claude;
    RefreshAgentSessionState(false);
    m_todoInputActive = false;
    SetActiveExpandedMode(ActiveExpandedMode::Agent);
    m_state = IslandState::Expanded;
    KillTimer(m_window.GetHWND(), m_displayTimerId);
    TransitionTo(IslandDisplayMode::AgentExpanded);
}

void DynamicIsland::CloseAgentPanel() {
    if (IsExpandedMode(ActiveExpandedMode::Agent)) {
        SetActiveExpandedMode(ActiveExpandedMode::None);
    }
    m_state = IslandState::Collapsed;
    TransitionTo(DetermineDisplayMode());
}

void DynamicIsland::SelectAgentSession(AgentKind kind, const std::wstring& sessionId) {
    m_selectedAgentKind = kind;
    m_selectedAgentSessionId = sessionId;
    m_selectedAgentHistory = sessionId.empty() ? std::vector<AgentHistoryEntry>{} : m_agentSessionMonitor.GetHistory(kind, sessionId);
    m_renderer.SetAgentSessionState(
        m_agentSessionSummaries,
        m_agentFilter,
        m_selectedAgentKind,
        m_selectedAgentSessionId,
        m_selectedAgentHistory,
        m_compactAgentKind,
        m_agentChooserOpen);
}

void DynamicIsland::HandleClaudeHookEvent(const ClaudeHookEvent& event) {
    m_claudeSessionStore.ProcessEvent(event);
    m_agentSessionMonitor.RequestRefresh();
    PostMessageW(m_window.GetHWND(), WM_AGENT_SESSIONS_UPDATED, 0, 0);
}

void DynamicIsland::HandleCodexHookEvent(const CodexHookEvent& event) {
    m_codexSessionStore.ProcessEvent(event);
    m_agentSessionMonitor.RequestRefresh();
    PostMessageW(m_window.GetHWND(), WM_AGENT_SESSIONS_UPDATED, 0, 0);
}

void DynamicIsland::HandleFaceUnlockEvent(const FaceUnlockEvent& event) {
    switch (event.kind) {
    case FaceUnlockEventKind::ScanStarted:
        ShowFaceUnlockFeedback(FaceIdState::Scanning, L"正在识别");
        break;
    case FaceUnlockEventKind::Success:
        ShowFaceUnlockFeedback(
            FaceIdState::Success,
            event.user.empty() ? L"欢迎回来" : (L"欢迎回来 " + event.user));
        break;
    case FaceUnlockEventKind::Failed:
        ShowFaceUnlockFeedback(
            FaceIdState::Failed,
            event.reason.empty() ? L"识别失败" : (L"识别失败: " + event.reason));
        break;
    }
}

void DynamicIsland::ShowFaceUnlockFeedback(FaceIdState state, const std::wstring& text) {
    KillTimer(m_window.GetHWND(), m_faceUnlockTimerId);
    m_faceUnlockFeedbackActive = state != FaceIdState::Hidden;
    m_faceUnlockVisualPending = m_faceUnlockFeedbackActive;
    m_faceUnlockVisualStarted = false;
    m_faceUnlockState = state;
    m_faceUnlockText = text;

    if (!m_faceUnlockFeedbackActive) {
        m_renderer.SetFaceUnlockState(FaceIdState::Hidden, L"");
        TransitionTo(DetermineDisplayMode());
        Invalidate(Dirty_FaceUnlock | Dirty_Region);
        return;
    }

    m_state = IslandState::Collapsed;
    TransitionTo(IslandDisplayMode::FaceUnlockFeedback);
    StartPendingFaceUnlockVisualIfReady();
    Invalidate(Dirty_FaceUnlock | Dirty_Region | Dirty_Hover);
}

void DynamicIsland::StartPendingFaceUnlockVisualIfReady() {
    if (!m_faceUnlockFeedbackActive || !m_faceUnlockVisualPending) {
        return;
    }

    const bool targetIsFaceUnlock =
        std::abs(GetTargetWidth() - Constants::Size::FACE_UNLOCK_WIDTH) < 0.5f &&
        std::abs(GetTargetHeight() - Constants::Size::FACE_UNLOCK_HEIGHT) < 0.5f;
    const bool currentIsFaceUnlock =
        std::abs(GetCurrentWidth() - Constants::Size::FACE_UNLOCK_WIDTH) < 1.0f &&
        std::abs(GetCurrentHeight() - Constants::Size::FACE_UNLOCK_HEIGHT) < 1.0f;

    if (!targetIsFaceUnlock || !currentIsFaceUnlock || !m_layoutController.IsSettled()) {
        if (!m_faceUnlockVisualStarted) {
            m_renderer.SetFaceUnlockState(FaceIdState::Hidden, L"");
        }
        return;
    }

    m_faceUnlockVisualPending = false;
    m_faceUnlockVisualStarted = true;
    m_renderer.SetFaceUnlockState(m_faceUnlockState, m_faceUnlockText);

    if (m_faceUnlockState == FaceIdState::Success) {
        SetTimer(m_window.GetHWND(), m_faceUnlockTimerId, 3000, nullptr);
    } else if (m_faceUnlockState == FaceIdState::Failed) {
        SetTimer(m_window.GetHWND(), m_faceUnlockTimerId, 1800, nullptr);
    }
}

void DynamicIsland::ClearFaceUnlockFeedback() {
    KillTimer(m_window.GetHWND(), m_faceUnlockTimerId);
    m_faceUnlockFeedbackActive = false;
    m_faceUnlockVisualPending = false;
    m_faceUnlockVisualStarted = false;
    m_faceUnlockState = FaceIdState::Hidden;
    m_faceUnlockText.clear();
    m_renderer.SetFaceUnlockState(FaceIdState::Hidden, L"");
    if (m_state == IslandState::Collapsed) {
        TransitionTo(DetermineDisplayMode());
    } else {
        StartAnimation();
    }
    Invalidate(Dirty_FaceUnlock | Dirty_Region);
}

void DynamicIsland::RunClaudeHookAction(int actionId) {
    ClaudeHookActionResult result;
    switch (actionId) {
    case 1:
        result = ClaudeHookInstaller::Install();
        break;
    case 2:
        result = ClaudeHookInstaller::Reinstall();
        break;
    case 3:
        result = ClaudeHookInstaller::Uninstall();
        break;
    default:
        return;
    }

    if (!result.message.empty()) {
        AlertInfo info{};
        info.type = 3;
        info.name = L"Claude Hooks";
        info.deviceType = result.message;
        EventBus::GetInstance().PublishNotificationArrived(info);
    }
}

bool DynamicIsland::HasWorkingClaudeSession() const {
    return std::any_of(m_agentSessionSummaries.begin(), m_agentSessionSummaries.end(), [](const AgentSessionSummary& summary) {
        return summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting;
    });
}

bool DynamicIsland::ShouldShowClaudeWorkingEdgeBadge(IslandDisplayMode mode) const {
    if (!HasWorkingClaudeSession()) {
        return false;
    }

    return mode == IslandDisplayMode::Idle ||
        mode == IslandDisplayMode::MusicCompact ||
        mode == IslandDisplayMode::PomodoroCompact ||
        mode == IslandDisplayMode::TodoListCompact;
}

float DynamicIsland::GetCompactTargetWidth(IslandDisplayMode mode) const {
    float width = Constants::Size::COMPACT_WIDTH;
    if (mode == IslandDisplayMode::PomodoroCompact) {
        width = Constants::Size::POMODORO_COMPACT_WIDTH;
    } else if (mode == IslandDisplayMode::MusicCompact) {
        width = Constants::Size::MUSIC_COMPACT_WIDTH;
    }

    if (ShouldShowClaudeWorkingEdgeBadge(mode)) {
        width += Constants::Size::AGENT_EDGE_BADGE_WIDTH;
    }
    return width;
}

float DynamicIsland::GetCompactTargetHeight(IslandDisplayMode mode) const {
    if (mode == IslandDisplayMode::MusicCompact) {
        return Constants::Size::MUSIC_COMPACT_HEIGHT;
    }
    if (mode == IslandDisplayMode::PomodoroCompact) {
        return Constants::Size::POMODORO_COMPACT_HEIGHT;
    }
    return Constants::Size::COMPACT_HEIGHT;
}

void DynamicIsland::RefreshAgentSessionState(bool preserveSelection) {
    const std::vector<AgentSessionSummary> fileSummaries = m_agentSessionMonitor.GetSummaries();
    m_claudeSessionStore.MergeHistoryFallback(fileSummaries);
    m_codexSessionStore.MergeHistoryFallback(fileSummaries);
    const std::vector<AgentSessionSummary> runtimeClaudeSummaries = m_claudeSessionStore.GetSummaries();
    const std::vector<AgentSessionSummary> runtimeCodexSummaries = m_codexSessionStore.GetSummaries();
    m_agentSessionSummaries.clear();
    m_agentSessionSummaries.reserve(fileSummaries.size() + runtimeClaudeSummaries.size() + runtimeCodexSummaries.size());

    for (const auto& summary : fileSummaries) {
        AgentSessionSummary merged = summary;
        merged.phase = summary.isLive ? AgentSessionPhase::Processing : AgentSessionPhase::Idle;
        merged.statusText = summary.kind == AgentKind::Claude
            ? (summary.isLive ? L"Working" : L"Idle")
            : (summary.isLive ? L"Active" : L"Idle");
        merged.providerSupportsHooks = true;

        if (summary.kind == AgentKind::Claude) {
            auto runtimeIt = std::find_if(runtimeClaudeSummaries.begin(), runtimeClaudeSummaries.end(), [&summary](const AgentSessionSummary& runtimeSummary) {
                return runtimeSummary.sessionId == summary.sessionId;
            });
            if (runtimeIt != runtimeClaudeSummaries.end()) {
                merged.phase = runtimeIt->phase;
                merged.statusText = runtimeIt->statusText;
                merged.pendingToolName = runtimeIt->pendingToolName;
                merged.pendingToolUseId = runtimeIt->pendingToolUseId;
                merged.recentActivityText = runtimeIt->recentActivityText;
                merged.providerSupportsHooks = runtimeIt->providerSupportsHooks;
                merged.lastActivityTs = (std::max)(merged.lastActivityTs, runtimeIt->lastActivityTs);
                merged.isLive = runtimeIt->isLive;
            }
        } else {
            auto runtimeIt = std::find_if(runtimeCodexSummaries.begin(), runtimeCodexSummaries.end(), [&summary](const AgentSessionSummary& runtimeSummary) {
                return runtimeSummary.sessionId == summary.sessionId;
            });
            if (runtimeIt != runtimeCodexSummaries.end()) {
                merged.phase = runtimeIt->phase;
                merged.statusText = runtimeIt->statusText;
                merged.recentActivityText = runtimeIt->recentActivityText;
                merged.providerSupportsHooks = runtimeIt->providerSupportsHooks;
                merged.lastActivityTs = (std::max)(merged.lastActivityTs, runtimeIt->lastActivityTs);
                merged.isLive = runtimeIt->isLive;
            }
        }

        m_agentSessionSummaries.push_back(std::move(merged));
    }

    for (const auto& runtimeSummary : runtimeClaudeSummaries) {
        const bool exists = std::any_of(m_agentSessionSummaries.begin(), m_agentSessionSummaries.end(), [&runtimeSummary](const AgentSessionSummary& summary) {
            return summary.kind == AgentKind::Claude && summary.sessionId == runtimeSummary.sessionId;
        });
        if (!exists) {
            m_agentSessionSummaries.push_back(runtimeSummary);
        }
    }

    for (const auto& runtimeSummary : runtimeCodexSummaries) {
        const bool exists = std::any_of(m_agentSessionSummaries.begin(), m_agentSessionSummaries.end(), [&runtimeSummary](const AgentSessionSummary& summary) {
            return summary.kind == AgentKind::Codex && summary.sessionId == runtimeSummary.sessionId;
        });
        if (!exists) {
            m_agentSessionSummaries.push_back(runtimeSummary);
        }
    }

    std::sort(m_agentSessionSummaries.begin(), m_agentSessionSummaries.end(), [](const AgentSessionSummary& lhs, const AgentSessionSummary& rhs) {
        if (lhs.kind != rhs.kind) {
            return lhs.kind == AgentKind::Claude;
        }
        if (lhs.lastActivityTs != rhs.lastActivityTs) {
            return lhs.lastActivityTs > rhs.lastActivityTs;
        }
        return lhs.sessionId < rhs.sessionId;
    });

    const bool hasClaude = std::any_of(m_agentSessionSummaries.begin(), m_agentSessionSummaries.end(), [](const AgentSessionSummary& summary) {
        return summary.kind == AgentKind::Claude;
    });
    const bool hasCodex = std::any_of(m_agentSessionSummaries.begin(), m_agentSessionSummaries.end(), [](const AgentSessionSummary& summary) {
        return summary.kind == AgentKind::Codex;
    });
    if (m_agentFilter == AgentSessionFilter::Claude && !hasClaude && hasCodex) {
        m_agentFilter = AgentSessionFilter::Codex;
    } else if (m_agentFilter == AgentSessionFilter::Codex && !hasCodex && hasClaude) {
        m_agentFilter = AgentSessionFilter::Claude;
    }

    if (m_compactAgentKind == AgentKind::Claude && !hasClaude && hasCodex) {
        m_compactAgentKind = AgentKind::Codex;
    } else if (m_compactAgentKind == AgentKind::Codex && !hasCodex && hasClaude) {
        m_compactAgentKind = AgentKind::Claude;
    }
    if (!(hasClaude && hasCodex)) {
        m_agentChooserOpen = false;
    }

    auto findMatchingSummary = [this](AgentKind kind, const std::wstring& sessionId) -> const AgentSessionSummary* {
        for (const auto& summary : m_agentSessionSummaries) {
            if (summary.kind == kind && summary.sessionId == sessionId && AgentSessionMatchesFilter(summary, m_agentFilter)) {
                return &summary;
            }
        }
        return nullptr;
    };

    const AgentSessionSummary* selectedSummary = nullptr;
    if (preserveSelection && !m_selectedAgentSessionId.empty()) {
        selectedSummary = findMatchingSummary(m_selectedAgentKind, m_selectedAgentSessionId);
    }

    if (!selectedSummary) {
        m_selectedAgentSessionId.clear();
        for (const auto& summary : m_agentSessionSummaries) {
            if (AgentSessionMatchesFilter(summary, m_agentFilter)) {
                m_selectedAgentKind = summary.kind;
                m_selectedAgentSessionId = summary.sessionId;
                selectedSummary = &summary;
                break;
            }
        }
    }

    if (selectedSummary) {
        m_selectedAgentHistory = m_agentSessionMonitor.GetHistory(selectedSummary->kind, selectedSummary->sessionId);
    } else {
        m_selectedAgentHistory.clear();
    }

    m_renderer.SetAgentSessionState(
        m_agentSessionSummaries,
        m_agentFilter,
        m_selectedAgentKind,
        m_selectedAgentSessionId,
        m_selectedAgentHistory,
        m_compactAgentKind,
        m_agentChooserOpen);
}

void DynamicIsland::PositionActiveImeWindow() {
    D2D1_RECT_F anchor = D2D1::RectF(0, 0, 0, 0);
    if (auto* todo = m_renderer.GetTodoComponent(); todo && IsTodoTextMode(DetermineDisplayMode())) {
        anchor = todo->GetImeAnchorRect();
    } else {
        anchor = m_renderer.GetImeAnchorRect();
    }
    if (anchor.right <= anchor.left || anchor.bottom <= anchor.top) {
        return;
    }

    HIMC himc = ImmGetContext(m_window.GetHWND());
    if (!himc) {
        return;
    }

    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_FORCE_POSITION;
    cf.ptCurrentPos.x = static_cast<LONG>(std::round(anchor.left * m_dpiScale));
    cf.ptCurrentPos.y = static_cast<LONG>(std::round(anchor.bottom * m_dpiScale));
    ImmSetCompositionWindow(himc, &cf);
    ImmReleaseContext(m_window.GetHWND(), himc);
}

bool DynamicIsland::IsTodoTextMode(IslandDisplayMode mode) const {
    return mode == IslandDisplayMode::TodoInputCompact || mode == IslandDisplayMode::TodoExpanded;
}

bool DynamicIsland::ShouldKeepCompactOverride() const {
    return m_hasCompactOverride &&
        m_compactOverrideMode != IslandDisplayMode::Idle &&
        IsCompactSwitchableMode(m_compactOverrideMode) &&
        CollectAvailableCompactModes().size() > 1;
}

void DynamicIsland::TransitionTo(IslandDisplayMode mode) {

	switch (mode) {

		case IslandDisplayMode::Shrunk:
			SetTargetSize(Constants::Size::SHRUNK_WIDTH, Constants::Size::SHRUNK_HEIGHT);
			break;

		case IslandDisplayMode::Idle:
			if (m_state == IslandState::Collapsed && m_shrunkWakeActive) {
				SetTargetSize(GetCompactTargetWidth(IslandDisplayMode::Idle), Constants::Size::COMPACT_HEIGHT);
			} else if (m_state == IslandState::Collapsed &&
				m_hasCompactOverride &&
				m_compactOverrideMode == IslandDisplayMode::Idle &&
				CollectAvailableCompactModes().size() > 1) {
				SetTargetSize(GetCompactTargetWidth(IslandDisplayMode::Idle), Constants::Size::COMPACT_HEIGHT);
			} else {
				SetTargetSize(Constants::Size::COLLAPSED_WIDTH, Constants::Size::COLLAPSED_HEIGHT);
			}

			break;

		case IslandDisplayMode::TodoInputCompact:
			SetTargetSize(Constants::Size::COMPACT_WIDTH, Constants::Size::COMPACT_HEIGHT);
			break;

		case IslandDisplayMode::TodoListCompact:
		case IslandDisplayMode::AgentCompact:
			SetTargetSize(GetCompactTargetWidth(mode), GetCompactTargetHeight(mode));
			break;

		case IslandDisplayMode::TodoExpanded:
			SetTargetSize(Constants::Size::TODO_EXPANDED_WIDTH, Constants::Size::TODO_EXPANDED_HEIGHT);
			break;

		case IslandDisplayMode::AgentExpanded:
			SetTargetSize(Constants::Size::AGENT_EXPANDED_WIDTH, Constants::Size::AGENT_EXPANDED_HEIGHT);
			break;

		case IslandDisplayMode::PomodoroCompact:
			SetTargetSize(GetCompactTargetWidth(IslandDisplayMode::PomodoroCompact), GetCompactTargetHeight(IslandDisplayMode::PomodoroCompact));
			break;

		case IslandDisplayMode::PomodoroExpanded:
			SetTargetSize(Constants::Size::POMODORO_EXPANDED_WIDTH, Constants::Size::POMODORO_EXPANDED_HEIGHT);
			break;

		case IslandDisplayMode::MusicCompact:
			SetTargetSize(GetCompactTargetWidth(IslandDisplayMode::MusicCompact), GetCompactTargetHeight(IslandDisplayMode::MusicCompact));

			break;

		case IslandDisplayMode::MusicExpanded:

			SetTargetSize(EXPANDED_WIDTH, m_mediaMonitor.HasSession() ? MUSIC_EXPANDED_HEIGHT : EXPANDED_HEIGHT);

			break;

		case IslandDisplayMode::Alert:

			SetTargetSize(Constants::Size::ALERT_WIDTH, Constants::Size::ALERT_HEIGHT);

			break;

		case IslandDisplayMode::FaceUnlockFeedback:
			SetTargetSize(Constants::Size::FACE_UNLOCK_WIDTH, Constants::Size::FACE_UNLOCK_HEIGHT);
			break;

		case IslandDisplayMode::Volume:

			SetTargetSize(Constants::Size::ALERT_WIDTH, Constants::Size::ALERT_HEIGHT);

			break;

		case IslandDisplayMode::WeatherExpanded:
			SetTargetSize(Constants::Size::WEATHER_EXPANDED_WIDTH, Constants::Size::WEATHER_EXPANDED_HEIGHT);
			break;

		case IslandDisplayMode::FileDrop:

			SetTargetSize(EXPANDED_WIDTH, EXPANDED_HEIGHT);

			break;

	}

	StartAnimation();

}

void DynamicIsland::SetTargetSize(float width, float height) {

	m_layoutController.SetTargetSize(width, height);

}

IslandDisplayMode DynamicIsland::DetermineBaseDisplayMode() const {
	for (const auto& entry : m_priorityTable) {
		if (entry.condition()) {
			return entry.mode;
		}
	}
	return IslandDisplayMode::Idle;
}

IslandDisplayMode DynamicIsland::DetermineDisplayMode() {
	const IslandDisplayMode baseMode = DetermineBaseDisplayMode();
	if (ShouldUseShrunkMode()) {
		return IslandDisplayMode::Shrunk;
	}
	if (m_state != IslandState::Collapsed || !IsCompactSwitchableMode(baseMode)) {
		return baseMode;
	}

	if (!m_hasCompactOverride) {
		return baseMode;
	}

	if (!IsCompactSwitchableMode(m_compactOverrideMode)) {
		ClearCompactOverride();
		return baseMode;
	}

	const auto availableModes = CollectAvailableCompactModes();
	if (std::find(availableModes.begin(), availableModes.end(), m_compactOverrideMode) == availableModes.end()) {
		ClearCompactOverride();
		return baseMode;
	}

	return m_compactOverrideMode;
}

std::vector<IslandDisplayMode> DynamicIsland::CollectAvailableCompactModes() const {
	std::vector<IslandDisplayMode> modes;
	for (IslandDisplayMode configuredMode : m_compactModeOrder) {
		switch (configuredMode) {
		case IslandDisplayMode::MusicCompact:
			if (m_mediaMonitor.IsPlaying()) {
				modes.push_back(configuredMode);
			}
			break;
		case IslandDisplayMode::PomodoroCompact:
			if (auto* pomodoro = m_renderer.GetPomodoroComponent(); pomodoro && pomodoro->HasActiveSession()) {
				modes.push_back(configuredMode);
			}
			break;
		case IslandDisplayMode::TodoListCompact:
			if (m_todoStore.HasItems()) {
				modes.push_back(configuredMode);
			}
			break;
		case IslandDisplayMode::AgentCompact: {
			const bool hasAgentCompactSession = std::any_of(
				m_agentSessionSummaries.begin(),
				m_agentSessionSummaries.end(),
				[](const AgentSessionSummary& summary) {
					return summary.kind == AgentKind::Claude || summary.kind == AgentKind::Codex;
				});
			if (hasAgentCompactSession) {
				modes.push_back(configuredMode);
			}
			break;
		}
		default:
			break;
		}
	}
	modes.push_back(IslandDisplayMode::Idle);
	return modes;
}

void DynamicIsland::ClearCompactOverride() {
	m_hasCompactOverride = false;
	m_compactOverrideMode = IslandDisplayMode::Idle;
}

bool DynamicIsland::IsCompactSwitchableMode(IslandDisplayMode mode) {
	return mode == IslandDisplayMode::Idle ||
		mode == IslandDisplayMode::TodoListCompact ||
		mode == IslandDisplayMode::AgentCompact ||
		mode == IslandDisplayMode::MusicCompact ||
		mode == IslandDisplayMode::PomodoroCompact;
}

SecondaryContentKind DynamicIsland::DetermineSecondaryContent() const {
	const bool isExpandedEnough = (GetCurrentHeight() >= Constants::Size::COMPACT_MIN_HEIGHT);
	if (m_isVolumeControlActive && isExpandedEnough && !m_isAlertActive) {
		return SecondaryContentKind::Volume;
	}
	if (m_fileStash.HasItems()) {
		return m_isDragHovering ? SecondaryContentKind::FileSwirlDrop : SecondaryContentKind::FileCircle;
	}
	return SecondaryContentKind::None;
}

D2D1_RECT_F DynamicIsland::GetSecondaryRectLogical() const {
	float secHeight = m_layoutController.GetSecondaryHeight();
	float secWidth = Constants::Size::SECONDARY_WIDTH;
	switch (DetermineSecondaryContent()) {
	case SecondaryContentKind::FileCircle:
	case SecondaryContentKind::FileSwirlDrop:
		secWidth = Constants::Size::FILE_CIRCLE_SIZE;
		break;
	case SecondaryContentKind::FileExpanded:
		secWidth = Constants::Size::FILE_SECONDARY_EXPANDED_WIDTH;
		break;
	case SecondaryContentKind::FileDropTarget:
		secWidth = Constants::Size::FILE_SECONDARY_DROPTARGET_WIDTH;
		break;
	default:
		break;
	}
	float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
	float top = Constants::UI::TOP_MARGIN;
	float bottom = top + GetCurrentHeight();
	float secLeft = (CANVAS_WIDTH - secWidth) / 2.0f;
	float secTop = bottom + 12.0f;
	if (DetermineSecondaryContent() == SecondaryContentKind::FileCircle ||
		DetermineSecondaryContent() == SecondaryContentKind::FileSwirlDrop) {
		secLeft = left + GetCurrentWidth() + Constants::Size::FILE_CIRCLE_GAP;
		secTop = top + (GetCurrentHeight() - secHeight) * 0.5f;
		if (secTop < top + 4.0f) {
			secTop = top + 4.0f;
		}
	}
	return D2D1::RectF(secLeft, secTop, secLeft + secWidth, secTop + secHeight);
}

HWND DynamicIsland::FindWindowBelowPoint(POINT screenPt) const {
	HWND self = m_window.GetHWND();
	HWND root = GetAncestor(self, GA_ROOT);
	HWND candidate = root ? GetWindow(root, GW_HWNDNEXT) : nullptr;
	while (candidate) {
		if (candidate != self && IsWindowVisible(candidate) && IsWindowEnabled(candidate) && !IsIconic(candidate)) {
			const LONG_PTR exStyle = GetWindowLongPtr(candidate, GWL_EXSTYLE);
			if ((exStyle & WS_EX_TRANSPARENT) == 0) {
				RECT rect{};
				if (GetWindowRect(candidate, &rect) && PtInRect(&rect, screenPt)) {
					HWND target = candidate;
					for (;;) {
						POINT childPt = screenPt;
						ScreenToClient(target, &childPt);
						HWND child = ChildWindowFromPointEx(
							target,
							childPt,
							CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
						if (!child || child == target) {
							break;
						}
						target = child;
					}
					return target;
				}
			}
		}
		candidate = GetWindow(candidate, GW_HWNDNEXT);
	}
	return nullptr;
}

bool DynamicIsland::ForwardMouseMessageToWindow(HWND target, UINT message, WPARAM wParam, POINT screenPt) const {
	if (!target || !IsWindow(target)) {
		return false;
	}

	LPARAM targetLParam = 0;
	if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
		targetLParam = MAKELPARAM(screenPt.x, screenPt.y);
	} else {
		POINT targetPt = screenPt;
		ScreenToClient(target, &targetPt);
		targetLParam = MAKELPARAM(targetPt.x, targetPt.y);
	}

	if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN) {
		if (HWND targetRoot = GetAncestor(target, GA_ROOT)) {
			SetForegroundWindow(targetRoot);
		}
	}

	SendMessage(target, message, wParam, targetLParam);
	return true;
}

bool DynamicIsland::ForwardMouseMessageToUnderlyingWindow(UINT message, WPARAM wParam, POINT screenPt) const {
	return ForwardMouseMessageToWindow(FindWindowBelowPoint(screenPt), message, wParam, screenPt);
}

void DynamicIsland::LoadConfig() {
	const std::wstring configPath = GetConfigPath();

	m_allowedApps.clear();

	auto getInt = [&](const wchar_t* section, const wchar_t* key, int fallback) {
		return GetPrivateProfileIntW(section, key, fallback, configPath.c_str());
	};

	CANVAS_WIDTH = (float)getInt(L"Settings", L"CanvasWidth", (int)Constants::Size::CANVAS_WIDTH);
	CANVAS_HEIGHT = (float)getInt(L"Settings", L"CanvasHeight", (int)Constants::Size::CANVAS_HEIGHT);
	CANVAS_WIDTH = (std::max)(CANVAS_WIDTH, Constants::Size::TODO_EXPANDED_WIDTH + 80.0f);
	if (CANVAS_HEIGHT < Constants::Size::CANVAS_HEIGHT) {
		CANVAS_HEIGHT = Constants::Size::CANVAS_HEIGHT;
	}

	EXPANDED_WIDTH = (float)getInt(L"MainUI", L"Width", (int)Constants::Size::EXPANDED_WIDTH);
	EXPANDED_HEIGHT = (float)getInt(L"MainUI", L"Height", (int)Constants::Size::EXPANDED_HEIGHT);
	MUSIC_EXPANDED_HEIGHT = EXPANDED_HEIGHT;
	m_mainUITransparency = getInt(L"MainUI", L"Transparency", 100) / 100.0f;
	m_filePanelTransparency = getInt(L"FilePanel", L"Transparency", 90) / 100.0f;
	m_springStiffness = (float)getInt(L"Advanced", L"SpringStiffness", 400);
	m_springDamping = (float)getInt(L"Advanced", L"SpringDamping", 30);
	m_fileStashMaxItems = getInt(L"Advanced", L"FileStashMaxItems", 5);
	m_mediaPollIntervalMs = getInt(L"Advanced", L"MediaPollIntervalMs", 1000);
	m_autoStart = AutoStart::IsEnabled();
	m_darkMode = getInt(L"Settings", L"DarkMode", 1) != 0;
	m_followSystemTheme = getInt(L"Settings", L"FollowSystemTheme", 1) != 0;
	wchar_t compactOrderBuf[256] = {};
	GetPrivateProfileStringW(L"MainUI", L"CompactModeOrder", L"Music,Pomodoro,Todo,Agent", compactOrderBuf, _countof(compactOrderBuf), configPath.c_str());
	LoadCompactModeOrder(compactOrderBuf);
	m_compactArtworkStyle = ParseArtworkStyle(ReadUtf8IniValue(configPath, L"MainUI", L"CompactAlbumArtStyle", L"Vinyl"), MusicArtworkStyle::Vinyl);
	m_expandedArtworkStyle = ParseArtworkStyle(ReadUtf8IniValue(configPath, L"MainUI", L"ExpandedAlbumArtStyle", L"Square"), MusicArtworkStyle::Square);

	wchar_t allowedAppsBuf[512] = { 0 };

	GetPrivateProfileStringW(L"Notifications", L"AllowedApps", L"", allowedAppsBuf, 512, configPath.c_str());
	if (allowedAppsBuf[0] == L'\0') {
		GetPrivateProfileStringW(L"Settings", L"AllowedApps", L"微信,QQ", allowedAppsBuf, 512, configPath.c_str());
	}
	std::wstring utf8AllowedApps = ReadUtf8IniValue(configPath, L"Notifications", L"AllowedApps", L"");
	if (utf8AllowedApps.empty()) {
		utf8AllowedApps = ReadUtf8IniValue(configPath, L"Settings", L"AllowedApps", L"");
	}

	std::wstring appsStr = utf8AllowedApps.empty() ? std::wstring(allowedAppsBuf) : utf8AllowedApps;

	size_t start = 0, end;

	while ((end = appsStr.find(L',', start)) != std::wstring::npos) {

		m_allowedApps.push_back(appsStr.substr(start, end - start));

		start = end + 1;

	}

	if (start < appsStr.size()) {
		m_allowedApps.push_back(appsStr.substr(start));
	}

	ApplyRuntimeSettings();
	m_layoutController.SetTargetSize(Constants::Size::COLLAPSED_WIDTH, Constants::Size::COLLAPSED_HEIGHT);

}

bool DynamicIsland::Initialize(HINSTANCE hInstance) {

	LoadConfig(); // 【新增】最优先加载配置文件

	// PR6: 初始化显示模式优先级调度表
	m_priorityTable = {
		{ IslandDisplayMode::FaceUnlockFeedback, 110, [this]() { return m_faceUnlockFeedbackActive; } },
		{ IslandDisplayMode::Alert,          100, [this]() { return m_isAlertActive; } },
		{ IslandDisplayMode::TodoExpanded,    95, [this]() { return IsExpandedMode(ActiveExpandedMode::Todo); } },
		{ IslandDisplayMode::AgentExpanded,   94, [this]() { return IsExpandedMode(ActiveExpandedMode::Agent); } },
		{ IslandDisplayMode::FileDrop,        93, [this]() { return m_isDragHovering; } },
		{ IslandDisplayMode::TodoInputCompact, 92, [this]() { return m_todoInputActive; } },
		{ IslandDisplayMode::WeatherExpanded,  90, [this]() { return IsExpandedMode(ActiveExpandedMode::Weather); } },
		{ IslandDisplayMode::PomodoroExpanded, 80, [this]() { return IsExpandedMode(ActiveExpandedMode::Pomodoro); } },
		{ IslandDisplayMode::MusicExpanded,    70, [this]() { return IsExpandedMode(ActiveExpandedMode::Music) && m_mediaMonitor.HasSession(); } },
		{ IslandDisplayMode::Volume,           60, [this]() { return m_isVolumeControlActive; } },
		{ IslandDisplayMode::MusicCompact,     50, [this]() { return m_state == IslandState::Collapsed && m_mediaMonitor.IsPlaying(); } },
		{ IslandDisplayMode::PomodoroCompact,  45, [this]() {
			auto* pomodoro = m_renderer.GetPomodoroComponent();
			return pomodoro && pomodoro->HasActiveSession() && !IsExpandedMode(ActiveExpandedMode::Pomodoro);
		} },
		{ IslandDisplayMode::Idle,             10, [this]() { return true; } },
	};

	// 获取系统初始 DPI 并计算缩放

	m_currentDpi = GetDpiForSystem();

	m_dpiScale = m_currentDpi / 96.0f;

	// 计算初始的物理像素大小

	int physCanvasW = (int)(CANVAS_WIDTH * m_dpiScale);

	int physCanvasH = (int)(CANVAS_HEIGHT * m_dpiScale);

	// 创建窗口（传入物理尺寸）

	if (!m_window.Create(hInstance, physCanvasW, physCanvasH, this)) return false;

	// 窗口创建后，获取该窗口所在显示器的精准 DPI

	m_currentDpi = GetDpiForWindow(m_window.GetHWND());

	m_dpiScale = m_currentDpi / 96.0f;

	// 初始化渲染引擎，传入物理尺寸

	if (!m_renderer.Initialize(m_window.GetHWND(), physCanvasW, physCanvasH)) return false;

	m_renderer.SetDpi((float)m_currentDpi);
	m_renderer.SetClockClickCallback([this]() { OpenPomodoroPanel(); });
	m_todoStore.Load();
	m_fileStash.Load();
	while (m_fileStash.Count() > 1) {
		m_fileStash.RemoveIndex(m_fileStash.Count() - 1);
	}
	if (m_fileStash.HasItems()) {
		m_fileSelectedIndex = 0;
	}
	m_renderer.SetTodoStore(&m_todoStore);
	if (auto* todo = m_renderer.GetTodoComponent()) {
		todo->SetOnRequestCloseInput([this]() { CloseTodoInputCompact(); });
		todo->SetOnRequestCloseExpanded([this]() { CloseTodoPanel(); });
		todo->SetOnLaunchComplete([this]() { OpenTodoInputCompact(); });
	}
	if (auto* agent = m_renderer.GetAgentSessionsComponent()) {
		agent->SetOnRequestOpenExpanded([this]() { OpenAgentPanel(); });
		agent->SetOnRequestCloseExpanded([this]() { CloseAgentPanel(); });
		agent->SetOnFilterChanged([this](AgentSessionFilter filter) {
			m_agentFilter = filter;
			RefreshAgentSessionState(false);
			Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
		});
		agent->SetOnCompactProviderChanged([this](AgentKind kind) {
			m_compactAgentKind = kind;
			m_agentFilter = (kind == AgentKind::Codex) ? AgentSessionFilter::Codex : AgentSessionFilter::Claude;
			RefreshAgentSessionState(false);
			Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
		});
		agent->SetOnCompactChooserOpenChanged([this](bool open) {
			m_agentChooserOpen = open;
			RefreshAgentSessionState(true);
			Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
		});
		agent->SetOnSessionSelected([this](AgentKind kind, const std::wstring& sessionId) {
			SelectAgentSession(kind, sessionId);
			Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
		});
		agent->SetOnApprovePermission([this](const std::wstring& toolUseId) {
			if (m_claudeSessionStore.RespondToPermission(m_claudeHookBridge, toolUseId, L"allow")) {
				RefreshAgentSessionState(true);
				Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
			}
		});
		agent->SetOnDenyPermission([this](const std::wstring& toolUseId) {
			if (m_claudeSessionStore.RespondToPermission(m_claudeHookBridge, toolUseId, L"deny", L"Denied by user via DynamicIsland")) {
				RefreshAgentSessionState(true);
				Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
			}
		});
	}
	if (auto* pomodoro = m_renderer.GetPomodoroComponent()) {
		pomodoro->SetOnFinished([this]() { HandlePomodoroFinished(); });
	}
	m_renderer.SetTheme(m_darkMode, m_mainUITransparency, m_filePanelTransparency);

    // 初始化文件面板窗口
    m_mediaMonitor.SetTargetWindow(m_window.GetHWND());
    m_settingsWindow.Create(hInstance, m_window.GetHWND());
    m_faceUnlockBridge.Start(m_window.GetHWND());

	m_mediaMonitor.Initialize();

	m_connectionMonitor.Initialize(m_window.GetHWND());

	m_notificationMonitor.Initialize(m_window.GetHWND(), m_allowedApps);

	m_lyricsMonitor.Initialize(m_window.GetHWND());

	m_systemMonitor.Initialize(m_window.GetHWND()); // 【新增】启动电量监控
	m_agentSessionMonitor.Initialize(m_window.GetHWND());
	m_claudeHookBridge.Start([this](const ClaudeHookEvent& event) {
		HandleClaudeHookEvent(event);
	});
	m_codexHookBridge.Start([this](const CodexHookEvent& event) {
		HandleCodexHookEvent(event);
	});
	RefreshAgentSessionState(false);

	// 注册 WeatherPlugin 的 fetch 完成通知
	if (m_systemMonitor.GetWeatherPlugin())
		m_systemMonitor.GetWeatherPlugin()->SetNotifyHwnd(m_window.GetHWND());

	// 【新增】立即获取天气（不等10分钟定时器）
	m_systemMonitor.UpdateWeather();

	m_weatherLocationText = m_systemMonitor.GetWeatherLocationText();
	m_weatherDesc = m_systemMonitor.GetWeatherDescription();

	m_weatherTemp = m_systemMonitor.GetWeatherTemperature();
RegisterHotKey(m_window.GetHWND(), HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'I');

	bool isPlaying = m_mediaMonitor.IsPlaying();

	bool hasSession = m_mediaMonitor.HasSession();

	// Construct RenderContext for initial draw

	RenderContext initCtx;

	initCtx.islandWidth = GetCurrentWidth();
	initCtx.islandHeight = GetCurrentHeight();
	initCtx.canvasWidth = CANVAS_WIDTH;
	initCtx.contentAlpha = GetCurrentAlpha();
	initCtx.mode = IslandDisplayMode::Idle;
	initCtx.currentTimeMs = GetTickCount64();

	m_renderer.SetPlaybackState(hasSession, isPlaying, 0.0f, m_mediaMonitor.GetTitle(), m_mediaMonitor.GetArtist());
	m_renderer.SetMusicArtworkStyles(m_compactArtworkStyle, m_expandedArtworkStyle);
	m_renderer.SetLyricData({ L"", -1, -1, 0 });
	m_renderer.SetWaveformState(m_mediaMonitor.GetWaveformBands(), GetCurrentHeight(), WaveformDisplayStyle::Compact);
	m_renderer.SetTimeData(false, L"");
	m_renderer.SetVolumeState(false, 0.0f);
	m_renderer.SetFileState(SecondaryContentKind::None, m_fileStash.Items(), -1, -1);
	const bool weatherAvailable = m_systemMonitor.GetWeatherPlugin() ? m_systemMonitor.GetWeatherPlugin()->IsAvailable() : false;
	m_renderer.SetWeatherState(m_weatherLocationText, m_weatherDesc, m_weatherTemp, L"", {}, {}, false, m_weatherViewMode, weatherAvailable);
	m_renderer.SetAlertState(false, AlertInfo{});
	m_renderer.SetFaceUnlockState(FaceIdState::Hidden, L"");
	m_renderer.SetAgentSessionState(
		m_agentSessionSummaries,
		m_agentFilter,
		m_selectedAgentKind,
		m_selectedAgentSessionId,
		m_selectedAgentHistory,
		m_compactAgentKind,
		m_agentChooserOpen);

	m_renderer.DrawCapsule(initCtx);

	m_window.Show();

	UpdateWindowRegion();

	CreateTrayIcon();

	// 【新增】订阅EventBus事件 - MediaMetadataChanged事件处理

	EventBus::GetInstance().Subscribe(EventType::MediaMetadataChanged, [this](const Event& e) {

		try {
			ImageData* imgData = std::any_cast<ImageData*>(e.userData);
			if (imgData) {
				if (!imgData->data.empty()) {
					m_renderer.LoadAlbumArtFromMemory(imgData->data);
				}
				delete imgData;
			}
		} catch (const std::bad_any_cast&) {
		}

		// 更新歌词
		std::wstring realTitle = m_mediaMonitor.GetTitle();
		std::wstring realArtist = m_mediaMonitor.GetArtist();
		m_lyricsMonitor.UpdateSong(realTitle, realArtist);
        
        m_cachedTitle = realTitle;
        m_cachedArtist = realArtist;

		// 触发重绘
		PostMessage(m_window.GetHWND(), WM_APP_INVALIDATE, Dirty_MediaState, 0);
	});

	// 【新增】订阅EventBus事件 - NotificationArrived事件处理

	EventBus::GetInstance().Subscribe(EventType::NotificationArrived, [this](const Event& e) {

		try {
			AlertInfo info = std::any_cast<AlertInfo>(e.userData);
			
			// 【OPT-03】设置优先级
			info.priority = GetAlertPriority(info.type, info.name);
			
			// 【OPT-03】P0 最高优先级可打断当前动画
			if (info.priority == PRIORITY_P0_CRITICAL && m_isAlertActive) {
				ProcessAlertWithPriority(info);
				return;
			}
			
			m_alertQueue.push(info);
			ProcessNextAlert();
		} catch (const std::bad_any_cast&) {
		}
	});

    EventBus::GetInstance().Subscribe(EventType::MediaPlaybackStateChanged, [this](const Event& e) {
        m_cachedIsPlaying = (e.wParam != 0);
        PostMessage(m_window.GetHWND(), WM_APP_INVALIDATE, Dirty_MediaState | Dirty_AudioLevel, 0);
    });

    EventBus::GetInstance().Subscribe(EventType::MediaSessionChanged, [this](const Event& e) {
        m_cachedHasSession = (e.wParam != 0);
        PostMessage(m_window.GetHWND(), WM_APP_INVALIDATE, Dirty_MediaState, 0);
    });

    m_cachedIsPlaying = m_mediaMonitor.IsPlaying();
    m_cachedHasSession = m_mediaMonitor.HasSession();
    m_cachedTitle = m_mediaMonitor.GetTitle();
    m_cachedArtist = m_mediaMonitor.GetArtist();
	auto initialAlbumArt = m_mediaMonitor.GetAlbumArtDataCopy();
	if (!initialAlbumArt.empty()) {
		m_renderer.LoadAlbumArtFromMemory(initialAlbumArt);
	}

	return true;

}

void DynamicIsland::Run() {

	m_window.RunLoop();

}

void DynamicIsland::EnsureRenderLoopRunning() {
    if (!m_renderLoopActive) {
        m_renderLoopActive = true;
        m_idleFrameCount = 0;
        SetTimer(m_window.GetHWND(), m_timerId, 16, nullptr);
    }
}

void DynamicIsland::Invalidate(uint32_t flags) {
    m_dirtyFlags |= flags;
    EnsureRenderLoopRunning();
}

bool DynamicIsland::ShouldKeepRendering() const {
    if (m_shrinkAnimating) return true;
    if (!m_layoutController.IsSettled()) return true;       // 弹簧动画中
    if (m_cachedIsPlaying && m_state != IslandState::Alert) return true;  // 音频可视化
    if (m_renderer.HasActiveAnimations()) return true;      // 组件动画/滚动中
    if (m_isVolumeControlActive) return true;               // 音量条显示中
    if (m_isAlertActive) return true;                       // 通知显示中
    if (m_faceUnlockFeedbackActive) return true;
    if (m_isDraggingProgress) return true;                  // 拖动进度条
    if (m_isDragHovering) return true;                      // 文件拖拽悬停
    if (IsExpandedMode(ActiveExpandedMode::Weather)) return true;                   // 天气展开面板动画持续运行
    if (auto* pomodoro = m_renderer.GetPomodoroComponent(); pomodoro && pomodoro->HasActiveSession()) return true;
    return false;
}

void DynamicIsland::StartAnimation() {

	// 添加保护：只有目标真正改变才触发动画

	static float lastTargetWidth = 0;

	static float lastTargetHeight = 0;

	if (!IsAnimating() || GetTargetWidth() != lastTargetWidth || GetTargetHeight() != lastTargetHeight) {

		lastTargetWidth = GetTargetWidth();

		lastTargetHeight = GetTargetHeight();

		m_layoutController.StartAnimation();

		EnsureRenderLoopRunning();
	SetTimer(m_window.GetHWND(), m_fullscreenTimerId, 1000, nullptr); // 全屏检测定时器（每秒）

	}

}

void DynamicIsland::UpdatePhysics() {

	float realAudioLevel = 0.0f;
	if (m_cachedIsPlaying) {
		realAudioLevel = m_mediaMonitor.GetAudioLevel();
		m_smoothedAudio += (realAudioLevel - m_smoothedAudio) * 0.2f;
		realAudioLevel = m_smoothedAudio;
	} else {
		m_smoothedAudio = 0.0f;
	}

	bool isPlaying = m_cachedIsPlaying;
	bool hasSession = m_cachedHasSession;

	static ULONGLONG lastUpdate = GetTickCount64();

	ULONGLONG now = GetTickCount64();

	float deltaTime = (now - lastUpdate) / 1000.0f;

	lastUpdate = now;
	UpdateShrinkTransition(now);

	// 正常物理更新 - 使用弹簧动画系统
	SecondaryContentKind secondaryContent = DetermineSecondaryContent();
	switch (secondaryContent) {
	case SecondaryContentKind::Volume:
	case SecondaryContentKind::FileMini:
		m_layoutController.SetSecondaryTarget(Constants::Size::SECONDARY_HEIGHT, 1.0f);
		break;
	case SecondaryContentKind::FileCircle:
	case SecondaryContentKind::FileSwirlDrop:
		m_layoutController.SetSecondaryTarget(Constants::Size::FILE_CIRCLE_SIZE, 1.0f);
		break;
	case SecondaryContentKind::FileExpanded:
		m_layoutController.SetSecondaryTarget(Constants::Size::FILE_SECONDARY_EXPANDED_HEIGHT, 1.0f);
		break;
	case SecondaryContentKind::FileDropTarget:
		m_layoutController.SetSecondaryTarget(Constants::Size::FILE_SECONDARY_DROPTARGET_HEIGHT, 1.0f);
		break;
	case SecondaryContentKind::None:
	default:
		m_layoutController.SetSecondaryTarget(0.0f, 0.0f);
		break;
	}

	m_layoutController.UpdatePhysics();
	StartPendingFaceUnlockVisualIfReady();

	// 物理动画完成后，根据状态自动调整目标尺寸

	if (m_layoutController.IsAnimating() && m_state == IslandState::Collapsed) {

		// 如果正在播放且当前是Mini态或目标是Mini，自动展开到Compact

		if (m_mediaMonitor.IsPlaying() && GetTargetHeight() < Constants::Size::COMPACT_MIN_HEIGHT && !m_fileStash.HasItems()) {

			const IslandDisplayMode compactMode = DetermineDisplayMode();
			SetTargetSize(GetCompactTargetWidth(compactMode), GetCompactTargetHeight(compactMode));

			StartAnimation();

		}

	}

	std::wstring realTitle = m_cachedTitle;
	std::wstring realArtist = m_cachedArtist;

	const IslandDisplayMode mode = DetermineDisplayMode();

	// 仅当前台页就是 Idle 且主岛处于收缩态时显示时间天气页内容
	bool showTime = (m_state == IslandState::Collapsed &&
		(mode == IslandDisplayMode::Idle || mode == IslandDisplayMode::Shrunk || m_manualShrunk));

	// 节流：每1000ms更新一次时间字符串，避免频繁重绘导致抖动

	static ULONGLONG lastTimeStrUpdate = 0;

	static std::wstring cachedTimeStr = L"";

	ULONGLONG nowMs = GetTickCount64();

	if (showTime && (nowMs - lastTimeStrUpdate > 1000)) {

		cachedTimeStr = GetCurrentTimeString();

		lastTimeStrUpdate = nowMs;

	} else if (!showTime) {

		cachedTimeStr = L"";

		lastTimeStrUpdate = 0;

	}

	std::wstring timeStr = cachedTimeStr;

	// 获取音乐进度与歌词（节流到 100ms）
	static ULONGLONG lastProgressPoll = 0;
	static float cachedProgress = 0.0f;
	static LyricData cachedLyricData;
    
	auto duration = m_mediaMonitor.GetDuration();
	auto position = m_mediaMonitor.GetPosition();
	auto durationMs = m_mediaMonitor.GetDurationMs();
	auto rawPositionMs = m_mediaMonitor.GetPositionMs();

	static int64_t trackedRawPositionMs = 0;
	static ULONGLONG trackedRawTick = 0;
	const int64_t rawPositionCountMs = rawPositionMs.count();
	if (trackedRawTick == 0 || std::llabs(rawPositionCountMs - trackedRawPositionMs) > 250 || !isPlaying) {
		trackedRawPositionMs = rawPositionCountMs;
		trackedRawTick = nowMs;
	}
	int64_t displayPositionMs = trackedRawPositionMs;
	if (isPlaying && trackedRawTick > 0) {
		displayPositionMs += static_cast<int64_t>(nowMs - trackedRawTick);
	}
	if (durationMs.count() > 0) {
		displayPositionMs = (std::min)(displayPositionMs, durationMs.count());
	}

	if (nowMs - lastProgressPoll > 100) {
		lastProgressPoll = nowMs;
		cachedProgress = (durationMs.count() > 0)
			? static_cast<float>(displayPositionMs) / static_cast<float>(durationMs.count())
			: 0.0f;
		if (hasSession) {
			cachedLyricData = m_lyricsMonitor.GetLyricData(displayPositionMs);
		}
	}

	float progress = cachedProgress;
	LyricData currentLyricData = hasSession
		? m_lyricsMonitor.GetLyricData(displayPositionMs)
		: cachedLyricData;

	// 如果正在拖动或刚松开进度条，使用临时进度

	if (m_isDraggingProgress || m_justReleasedProgress) {

		progress = m_tempProgress;

		// 如果刚松开，检查是否应该停止使用临时进度

		if (m_justReleasedProgress) {

			ULONGLONG now = GetTickCount64();

			bool shouldStop = false;

			// 条件1：已经过了1秒

			if (now - m_progressReleaseTime > 1000) {

				shouldStop = true;

			}

			// 条件2：实际位置已经接近临时位置（差距小于0.5秒）

			else if (duration.count() > 0) {

				float actualProgress = (float)position.count() / (float)duration.count();

				float threshold = 0.5f / (float)duration.count();

				if (std::abs(actualProgress - m_tempProgress) < threshold) {

					shouldStop = true;

				}

			}

			if (shouldStop) {

				m_justReleasedProgress = false;

			}

		}

	}
	m_renderer.SetPlaybackState(hasSession, isPlaying, progress, realTitle, realArtist);
	m_renderer.SetMusicArtworkStyles(m_compactArtworkStyle, m_expandedArtworkStyle);
	m_renderer.SetMusicInteractionState(m_hoveredButtonIndex, m_pressedButtonIndex, m_hoveredProgress, m_pressedProgress);
	m_renderer.SetLyricData(currentLyricData);
	m_renderer.SetWaveformState(
		m_mediaMonitor.GetWaveformBands(),
		GetCurrentHeight(),
		mode == IslandDisplayMode::MusicExpanded ? WaveformDisplayStyle::Expanded : WaveformDisplayStyle::Compact);
	m_renderer.SetTimeData(showTime, timeStr);
	m_renderer.SetVolumeState(m_isVolumeControlActive, m_currentVolume);
	m_renderer.SetFileState(secondaryContent, m_fileStash.Items(), m_fileSelectedIndex, m_fileHoveredIndex);
	m_renderer.SetAlertState(m_isAlertActive, m_currentAlert);

	std::vector<HourlyForecast> hourlyForecasts;
	std::vector<DailyForecast> dailyForecasts;
	std::wstring weatherIconId = L"100";
	if (m_systemMonitor.GetWeatherPlugin()) {
		m_weatherLocationText = m_systemMonitor.GetWeatherPlugin()->GetLocationText();
		m_weatherDesc = m_systemMonitor.GetWeatherPlugin()->GetWeatherDescription();
		m_weatherTemp = m_systemMonitor.GetWeatherPlugin()->GetTemperature();
		weatherIconId = m_systemMonitor.GetWeatherPlugin()->GetIconId();
		hourlyForecasts = m_systemMonitor.GetWeatherPlugin()->GetHourlyForecast();
		dailyForecasts = m_systemMonitor.GetWeatherPlugin()->GetDailyForecast();
	}
	const bool weatherAvailable = m_systemMonitor.GetWeatherPlugin() ? m_systemMonitor.GetWeatherPlugin()->IsAvailable() : false;
	m_renderer.SetWeatherState(m_weatherLocationText, m_weatherDesc, m_weatherTemp, weatherIconId,
		hourlyForecasts, dailyForecasts, IsExpandedMode(ActiveExpandedMode::Weather), m_weatherViewMode, weatherAvailable);

	RenderContext ctx;
	ctx.islandWidth = GetCurrentWidth();
	ctx.islandHeight = GetCurrentHeight();
	ctx.canvasWidth = CANVAS_WIDTH;
	ctx.contentAlpha = GetCurrentAlpha();
	ctx.secondaryHeight = m_layoutController.GetSecondaryHeight();
	ctx.secondaryAlpha = m_layoutController.GetSecondaryAlpha();
	ctx.secondaryContent = secondaryContent;
	ctx.mode = mode;
	ctx.currentTimeMs = now;
	ctx.shrinkAnimating = m_shrinkAnimating;
	ctx.shrinkProgress = m_shrinkProgress;
	ctx.shrinkSourceMode = m_shrinkSourceMode;
	ctx.manualShrunk = m_manualShrunk;

	m_renderer.DrawCapsule(ctx);

	// 在主岛或副岛尺寸变化时更新 Region
	static float lastRegionW = 0, lastRegionH = 0, lastRegionSecH = 0;
	float curW = GetCurrentWidth(), curH = GetCurrentHeight();
	float curSecH = m_layoutController.GetSecondaryHeight();
	if (std::abs(curW - lastRegionW) > 0.5f ||
		std::abs(curH - lastRegionH) > 0.5f ||
		std::abs(curSecH - lastRegionSecH) > 0.5f) {
		UpdateWindowRegion();
		lastRegionW = curW;
		lastRegionH = curH;
		lastRegionSecH = curSecH;
	}

	// === 自适应渲染循环控制 ===
	if (ShouldKeepRendering()) {
		m_idleFrameCount = 0;
	} else {
		m_idleFrameCount++;
		if (m_idleFrameCount > IDLE_COOLDOWN) {
			KillTimer(m_window.GetHWND(), m_timerId);
			m_renderLoopActive = false;
			m_dirtyFlags = Dirty_None;
		}
	}

}

LRESULT DynamicIsland::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	switch (uMsg) {

	case WM_APP_INVALIDATE:
		m_dirtyFlags |= (uint32_t)wParam;
		EnsureRenderLoopRunning();
		return 0;

	case WM_FACE_UNLOCK_EVENT: {
		FaceUnlockEvent* event = reinterpret_cast<FaceUnlockEvent*>(lParam);
		if (event) {
			HandleFaceUnlockEvent(*event);
			delete event;
		}
		return 0;
	}

	case WM_SETTINGS_APPLY:
		LoadConfig();
		m_notificationMonitor.UpdateAllowedApps(m_allowedApps);
		m_systemMonitor.SetLowBatteryThreshold(
			GetPrivateProfileIntW(L"Advanced", L"LowBatteryThreshold", 20, GetConfigPath().c_str()));
		ApplyRuntimeSettings();
		m_systemMonitor.UpdateWeather();  // 异步 fetch，完成后会发 WM_WEATHER_UPDATED
		m_weatherLocationText = m_systemMonitor.GetWeatherLocationText();
		m_weatherDesc = m_systemMonitor.GetWeatherDescription();
		m_weatherTemp = m_systemMonitor.GetWeatherTemperature();
		if (IsExpandedMode(ActiveExpandedMode::Pomodoro)) {
			SyncPomodoroMode();
		} else {
			TransitionTo(DetermineDisplayMode());
		}
		Invalidate(Dirty_Weather | Dirty_MediaState | Dirty_Region);
		return 0;

	case WM_AGENT_SESSIONS_UPDATED:
		RefreshAgentSessionState(true);
		Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
		return 0;

	case WM_WEATHER_UPDATED: {
		// WeatherPlugin 异步 fetch 完成，刷新岛屿显示
		m_weatherLocationText = m_systemMonitor.GetWeatherLocationText();
		m_weatherDesc = m_systemMonitor.GetWeatherDescription();
		m_weatherTemp = m_systemMonitor.GetWeatherTemperature();
		if (m_systemMonitor.GetWeatherPlugin()) {
			auto* wp = m_systemMonitor.GetWeatherPlugin();
			m_renderer.SetWeatherState(m_weatherLocationText, m_weatherDesc, m_weatherTemp, wp->GetIconId(),
				wp->GetHourlyForecast(), wp->GetDailyForecast(),
				IsExpandedMode(ActiveExpandedMode::Weather), m_weatherViewMode, wp->IsAvailable());
		}
		Invalidate(Dirty_Weather | Dirty_Region);
		return 0;
	}

	case WM_NCHITTEST: {
		POINT physicalPt;
		physicalPt.x = GET_X_LPARAM(lParam);
		physicalPt.y = GET_Y_LPARAM(lParam);
		ScreenToClient(hwnd, &physicalPt);

		const POINT pt = LogicalFromPhysical(physicalPt);

		if (DetermineDisplayMode() == IslandDisplayMode::Shrunk && !m_isDragHovering) {
			SecondaryContentKind secondary = DetermineSecondaryContent();
			if (secondary != SecondaryContentKind::None) {
				D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
				if ((float)pt.x >= secondaryRect.left && (float)pt.x <= secondaryRect.right &&
					(float)pt.y >= secondaryRect.top && (float)pt.y <= secondaryRect.bottom) {
					return HTCLIENT;
				}
			}
			if (HitTestShrunkHandle(pt)) {
				return HTCLIENT;
			}
			return HTCLIENT;
		}

		float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
		float right = left + GetCurrentWidth();
		float top = Constants::UI::TOP_MARGIN;
		float bottom = top + GetCurrentHeight();
		if (pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom) {
			return HTCLIENT;
		}

		SecondaryContentKind secondary = DetermineSecondaryContent();
		if (secondary != SecondaryContentKind::None) {
			D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
			if ((float)pt.x >= secondaryRect.left && (float)pt.x <= secondaryRect.right &&
				(float)pt.y >= secondaryRect.top && (float)pt.y <= secondaryRect.bottom) {
				return HTCLIENT;
			}
		}

		Invalidate(Dirty_Hover);
		return HTTRANSPARENT;
	}

	case WM_MOUSEMOVE:

	{

		POINT physicalPt;

		physicalPt.x = GET_X_LPARAM(lParam);

		physicalPt.y = GET_Y_LPARAM(lParam);

		const POINT pt = LogicalFromPhysical(physicalPt);

		if (m_forwardMouseDragActive) {
			POINT screenPt = physicalPt;
			ClientToScreen(hwnd, &screenPt);
			if (!ForwardMouseMessageToWindow(m_forwardMouseTarget, WM_MOUSEMOVE, wParam, screenPt)) {
				m_forwardMouseDragActive = false;
				m_forwardMouseTarget = nullptr;
				if (GetCapture() == hwnd) {
					ReleaseCapture();
				}
			}
			return 0;
		}

		IslandDisplayMode currentMode = DetermineDisplayMode();
		if (currentMode == IslandDisplayMode::Shrunk && !HitTestShrunkHandle(pt)) {
			POINT screenPt = physicalPt;
			ClientToScreen(hwnd, &screenPt);
			if ((wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0) {
				m_forwardMouseTarget = FindWindowBelowPoint(screenPt);
				m_forwardMouseDragActive = m_forwardMouseTarget != nullptr;
				if (m_forwardMouseDragActive && GetCapture() != hwnd) {
					SetCapture(hwnd);
				}
				ForwardMouseMessageToWindow(m_forwardMouseTarget, WM_MOUSEMOVE, wParam, screenPt);
				return 0;
			}
		}

		if (HandleFileSecondaryMouseMove(hwnd, pt, wParam)) {
			return 0;
		}

		const bool todoBadgeHoverChanged =
			(currentMode == IslandDisplayMode::Idle) ? m_renderer.HandleIdleTodoMouseMove((float)pt.x, (float)pt.y) : false;
		if ((currentMode == IslandDisplayMode::PomodoroExpanded ||
			currentMode == IslandDisplayMode::PomodoroCompact ||
			currentMode == IslandDisplayMode::TodoInputCompact ||
			currentMode == IslandDisplayMode::TodoListCompact ||
			currentMode == IslandDisplayMode::TodoExpanded ||
			currentMode == IslandDisplayMode::AgentCompact ||
			currentMode == IslandDisplayMode::AgentExpanded) &&
			m_renderer.OnMouseMove((float)pt.x, (float)pt.y)) {
			Invalidate(Dirty_Hover | Dirty_Time);
			return 0;
		}

		// 鼠标悬停检测

		float hoverLeft = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;

		float hoverRight = hoverLeft + GetCurrentWidth();

		float hoverTop = Constants::UI::TOP_MARGIN;

		float hoverBottom = hoverTop + GetCurrentHeight();

		bool isOverIsland = (pt.x >= hoverLeft && pt.x <= hoverRight && pt.y >= hoverTop && pt.y <= hoverBottom);

		if (isOverIsland != m_isHovering) {

			m_isHovering = isOverIsland;

			// 悬停时：停止待处理的自动收缩定时器

			if (m_isHovering) {

				KillTimer(m_window.GetHWND(), m_displayTimerId);

			}

			bool suppressHoverResize = m_fileStash.HasItems();
			if (auto* pomodoro = m_renderer.GetPomodoroComponent()) {
				suppressHoverResize = suppressHoverResize || pomodoro->HasActiveSession();
			}
			suppressHoverResize = suppressHoverResize || m_todoInputActive;
			const bool keepCompactOverride = ShouldKeepCompactOverride();

			

			if (!suppressHoverResize && m_isHovering && m_state == IslandState::Collapsed) {

				if (GetTargetHeight() < Constants::Size::COMPACT_MIN_HEIGHT) {

					const IslandDisplayMode compactMode = DetermineDisplayMode();
					SetTargetSize(GetCompactTargetWidth(compactMode), GetCompactTargetHeight(compactMode));

					StartAnimation();

				}

			} else if (!suppressHoverResize && !keepCompactOverride && !m_isHovering && m_state == IslandState::Collapsed) {

				// 鼠标离开时：如果是compact大小，启动5秒定时器后缩小到mini

				if (GetTargetHeight() >= Constants::Size::COMPACT_MIN_HEIGHT) {

					// 先启动定时器，5秒后会缩小

					SetTimer(m_window.GetHWND(), m_displayTimerId, 5000, nullptr);

				}

			}

		}

		// Weather icon hover detection: trigger animation on first hover
		if (m_state == IslandState::Collapsed && currentMode == IslandDisplayMode::Idle && !IsExpandedMode(ActiveExpandedMode::Weather)) {
			float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
			float right = left + GetCurrentWidth();
			float top = Constants::UI::TOP_MARGIN;
			float islandHeight = GetCurrentHeight();
			float iconSize = islandHeight * 0.4f;
			float iconX = right - iconSize - 15.0f;
			float iconY = top + (islandHeight - iconSize) / 2.0f;

			bool isOverWeather = (pt.x >= iconX - 40.0f && pt.x <= iconX + iconSize &&
			                      pt.y >= top && pt.y <= top + islandHeight);

			if (isOverWeather) {
				EnsureRenderLoopRunning();
			}
		}

		if (currentMode == IslandDisplayMode::TodoInputCompact ||
			currentMode == IslandDisplayMode::TodoListCompact ||
			currentMode == IslandDisplayMode::TodoExpanded) {
			TRACKMOUSEEVENT tmeTodo{};
			tmeTodo.cbSize = sizeof(TRACKMOUSEEVENT);
			tmeTodo.dwFlags = TME_LEAVE;
			tmeTodo.hwndTrack = hwnd;
			TrackMouseEvent(&tmeTodo);
			if (todoBadgeHoverChanged) {
				Invalidate(Dirty_Hover | Dirty_Region);
			}
			return 0;
		}

// 进度条拖动

		if (m_isDraggingProgress) {

			const auto progressLayout = GetProgressBarLayout();
			const float progress = ((float)pt.x - progressLayout.left) / (progressLayout.right - progressLayout.left);
			m_tempProgress = ClampProgress(progress);

			StartAnimation();

			return 0;

		}

		if (m_dragging) {
			m_dragging = false;
			ReleaseCapture();
			return 0;
		}

		int hit = m_layoutController.HitTestPlaybackButtons(pt, currentMode == IslandDisplayMode::MusicExpanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);

		if (hit != m_hoveredButtonIndex) {

			m_hoveredButtonIndex = hit;

			StartAnimation(); // 叫醒渲染器画高亮

		}

		// 进度条悬停检测

		int progressHit = m_layoutController.HitTestProgressBar(pt, currentMode == IslandDisplayMode::MusicExpanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);

		if (progressHit != m_hoveredProgress) {

			m_hoveredProgress = progressHit;

			StartAnimation();

		}

		TRACKMOUSEEVENT tme{};

		tme.cbSize = sizeof(TRACKMOUSEEVENT);

		tme.dwFlags = TME_LEAVE;

		tme.hwndTrack = hwnd;

		TrackMouseEvent(&tme);
		if (todoBadgeHoverChanged) {
			Invalidate(Dirty_Hover | Dirty_Region);
		}

		return 0;

	}

	case WM_MOUSELEAVE: {

		if (m_hoveredButtonIndex != -1 || m_pressedButtonIndex != -1 || m_hoveredProgress != -1 || m_pressedProgress != -1) {

			m_hoveredButtonIndex = -1;

			m_pressedButtonIndex = -1;

			m_hoveredProgress = -1;

			m_pressedProgress = -1;

		}

		// 鼠标离开时重置悬停状态

		if (m_isHovering) {

			m_isHovering = false;

		
			// Auto-collapse from COMPACT to MINI when mouse leaves island
			bool keepCompact = false;
			if (auto* pomodoro = m_renderer.GetPomodoroComponent()) {
				keepCompact = pomodoro->HasActiveSession();
			}
			keepCompact = keepCompact || ShouldKeepCompactOverride() || m_todoInputActive;
			if (m_state == IslandState::Collapsed &&
			    GetTargetHeight() >= Constants::Size::COMPACT_MIN_HEIGHT &&
			    !keepCompact) {
			    SetTimer(hwnd, m_displayTimerId, 5000, nullptr);
			}
		}

		if (m_fileHoveredIndex != -1) {
			m_fileHoveredIndex = -1;
		}
		m_renderer.HandleIdleTodoMouseMove(-1.0f, -1.0f);
		Invalidate(Dirty_Hover | Dirty_FileDrop);
		return 0;

	}

	case WM_CAPTURECHANGED:
		if (m_forwardMouseDragActive && reinterpret_cast<HWND>(lParam) != hwnd) {
			m_forwardMouseDragActive = false;
			m_forwardMouseTarget = nullptr;
		}
		return 0;

	case WM_KILLFOCUS:
		if (m_todoInputActive) {
			CloseTodoInputCompact();
			Invalidate(Dirty_Time | Dirty_Region);
		}
		return 0;

	// 鼠标点击胶囊体

	case WM_LBUTTONDOWN: {

		POINT physicalPt;

		physicalPt.x = GET_X_LPARAM(lParam);

		physicalPt.y = GET_Y_LPARAM(lParam);

		const POINT pt = LogicalFromPhysical(physicalPt);

		if (HandleFileSecondaryMouseDown(pt)) {
			return 0;
		}

		IslandDisplayMode currentMode = DetermineDisplayMode();
		if (currentMode == IslandDisplayMode::Shrunk) {
			if (HitTestShrunkHandle(pt)) {
				const IslandDisplayMode wakeMode = IsCompactSwitchableMode(m_shrinkSourceMode)
					? m_shrinkSourceMode
					: IslandDisplayMode::Idle;
				BeginShrinkTransition(false, wakeMode);
				m_manualShrunk = false;
				m_shrunkWakeActive = true;
				m_hasCompactOverride = true;
				m_compactOverrideMode = wakeMode;
				TransitionTo(wakeMode);
				Invalidate(Dirty_Time | Dirty_Region | Dirty_Hover);
				return 0;
			}
			POINT screenPt = physicalPt;
			ClientToScreen(hwnd, &screenPt);
			m_forwardMouseTarget = FindWindowBelowPoint(screenPt);
			m_forwardMouseDragActive = m_forwardMouseTarget != nullptr;
			if (m_forwardMouseDragActive && GetCapture() != hwnd) {
				SetCapture(hwnd);
			}
			if (!ForwardMouseMessageToWindow(m_forwardMouseTarget, WM_LBUTTONDOWN, wParam, screenPt)) {
				m_forwardMouseDragActive = false;
				m_forwardMouseTarget = nullptr;
				if (GetCapture() == hwnd) {
					ReleaseCapture();
				}
			}
			return 0;
		}
		if (HitTestCompactShrinkHandle(pt, currentMode)) {
			BeginShrinkTransition(true, currentMode);
			m_manualShrunk = true;
			m_shrunkWakeActive = false;
			m_isHovering = false;
			KillTimer(hwnd, m_displayTimerId);
			TransitionTo(IslandDisplayMode::Shrunk);
			Invalidate(Dirty_Time | Dirty_Region | Dirty_Hover);
			return 0;
		}
		if (currentMode == IslandDisplayMode::Idle) {
			if (auto* todo = m_renderer.GetTodoComponent(); todo && todo->IsLaunchAnimating()) {
				return 0;
			}
		}
		if (currentMode == IslandDisplayMode::Idle &&
			m_renderer.IdleTodoBadgeContains((float)pt.x, (float)pt.y)) {
			if (auto* todo = m_renderer.GetTodoComponent()) {
				todo->BeginIdleOpenAnimation();
			}
			Invalidate(Dirty_Hover | Dirty_Time | Dirty_Region);
			return 0;
		}
		if ((currentMode == IslandDisplayMode::Idle ||
			currentMode == IslandDisplayMode::PomodoroExpanded ||
			currentMode == IslandDisplayMode::PomodoroCompact ||
			currentMode == IslandDisplayMode::TodoInputCompact ||
			currentMode == IslandDisplayMode::TodoListCompact ||
			currentMode == IslandDisplayMode::TodoExpanded ||
			currentMode == IslandDisplayMode::AgentCompact ||
			currentMode == IslandDisplayMode::AgentExpanded) &&
			m_renderer.OnMouseClick((float)pt.x, (float)pt.y)) {
			if (currentMode != IslandDisplayMode::Idle) {
				if (currentMode == IslandDisplayMode::PomodoroExpanded || currentMode == IslandDisplayMode::PomodoroCompact) {
					SyncPomodoroMode();
				} else if (currentMode == IslandDisplayMode::TodoListCompact) {
					OpenTodoPanel();
				}
			}
			if (IsTodoTextMode(DetermineDisplayMode())) {
				PositionActiveImeWindow();
			}
			Invalidate(Dirty_Hover | Dirty_Time | Dirty_Region);
			return 0;
		}

		int hit = m_layoutController.HitTestPlaybackButtons(pt, currentMode == IslandDisplayMode::MusicExpanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);

		if (hit != -1) {

			m_pressedButtonIndex = hit;

			StartAnimation(); // 触发缩小（按下）动画

			return 0; // 拦截点击，不要触发窗口拖拽！

		}

		// 点击胶囊不再打开文件 - 文件在侧边栏管理

		// --- 检查是否点中了进度条 ---

		int progressHit = m_layoutController.HitTestProgressBar(pt, currentMode == IslandDisplayMode::MusicExpanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);

		if (progressHit != -1) {

			m_isDraggingProgress = true;

			const auto progressLayout = GetProgressBarLayout();
			const float progress = ((float)pt.x - progressLayout.left) / (progressLayout.right - progressLayout.left);
			m_tempProgress = ClampProgress(progress);

			StartAnimation();

			return 0;

		}

		// --- 检查是否点中了天气图标 ---
		if (m_state == IslandState::Collapsed && currentMode == IslandDisplayMode::Idle && !IsExpandedMode(ActiveExpandedMode::Weather)) {
			float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
			float right = left + GetCurrentWidth();
			float top = Constants::UI::TOP_MARGIN;
			float islandHeight = GetCurrentHeight();
			float iconSize = islandHeight * 0.4f;
			float iconX = right - iconSize - 15.0f;
			float iconY = top + (islandHeight - iconSize) / 2.0f;

			// 扩大点击区域，包含图标和温度文字
			bool isOverWeather = (pt.x >= iconX - 40.0f && pt.x <= iconX + iconSize &&
			                      pt.y >= top && pt.y <= top + islandHeight);

			if (isOverWeather) {
				SetActiveExpandedMode(ActiveExpandedMode::Weather);
				m_state = IslandState::Expanded; // 必须将状态置为Expanded，这样收缩时才能正确处理
				TransitionTo(IslandDisplayMode::WeatherExpanded);
				return 0;
			}
		}

		if (m_state == IslandState::Collapsed) {
			// 仅在当前确实处于音乐紧凑态时，点击主岛才展开到音乐面板
			if (currentMode == IslandDisplayMode::MusicCompact) {
				m_state = IslandState::Expanded;
				TransitionTo(IslandDisplayMode::MusicExpanded);
				SetActiveExpandedMode(ActiveExpandedMode::Music);
				m_mediaMonitor.RequestAlbumArtRefresh();

				if (m_systemMonitor.GetWeatherPlugin()) {
					m_systemMonitor.GetWeatherPlugin()->RequestRefresh();
				}
			}

		} else {

			// 从 Expanded 状态点击 → Compact（不经过 Mini）

			m_state = IslandState::Collapsed;
			SetActiveExpandedMode(ActiveExpandedMode::None);

			// 【修复】点击收缩时，立即关闭副岛音量显示，防止收缩后主岛变成音量条
			if (m_isVolumeControlActive) {
				m_isVolumeControlActive = false;
				KillTimer(hwnd, m_volumeTimerId);
			}

			IslandDisplayMode nextMode = DetermineDisplayMode();
			if (nextMode == IslandDisplayMode::Idle) {
				SetTargetSize(GetCompactTargetWidth(IslandDisplayMode::Idle), Constants::Size::COMPACT_HEIGHT);
				StartAnimation();
				SetTimer(hwnd, m_displayTimerId, 5000, nullptr);
			} else {
				TransitionTo(nextMode);
			}

		}

		return 0;

	}

	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP: {
		POINT physicalPt;
		physicalPt.x = GET_X_LPARAM(lParam);
		physicalPt.y = GET_Y_LPARAM(lParam);
		const POINT pt = LogicalFromPhysical(physicalPt);
		if (DetermineDisplayMode() == IslandDisplayMode::Shrunk && !HitTestShrunkHandle(pt)) {
			POINT screenPt = physicalPt;
			ClientToScreen(hwnd, &screenPt);
			if (uMsg == WM_RBUTTONDOWN) {
				m_forwardMouseTarget = FindWindowBelowPoint(screenPt);
				m_forwardMouseDragActive = m_forwardMouseTarget != nullptr;
				if (m_forwardMouseDragActive && GetCapture() != hwnd) {
					SetCapture(hwnd);
				}
			}
			ForwardMouseMessageToWindow(m_forwardMouseTarget ? m_forwardMouseTarget : FindWindowBelowPoint(screenPt), uMsg, wParam, screenPt);
			if (uMsg == WM_RBUTTONUP) {
				m_forwardMouseDragActive = false;
				m_forwardMouseTarget = nullptr;
				if (GetCapture() == hwnd) {
					ReleaseCapture();
				}
			}
			return 0;
		}
		break;
	}

	case WM_LBUTTONUP:

	{

		POINT physicalPt;

		physicalPt.x = GET_X_LPARAM(lParam);

		physicalPt.y = GET_Y_LPARAM(lParam);

		const POINT pt = LogicalFromPhysical(physicalPt);

		if (m_forwardMouseDragActive) {
			POINT screenPt = physicalPt;
			ClientToScreen(hwnd, &screenPt);
			ForwardMouseMessageToWindow(m_forwardMouseTarget, WM_LBUTTONUP, wParam, screenPt);
			m_forwardMouseDragActive = false;
			m_forwardMouseTarget = nullptr;
			if (GetCapture() == hwnd) {
				ReleaseCapture();
			}
			return 0;
		}

		if (HandleFileSecondaryMouseUp(pt)) {
			return 0;
		}
		if (DetermineDisplayMode() == IslandDisplayMode::Shrunk && !HitTestShrunkHandle(pt)) {
			POINT screenPt = physicalPt;
			ClientToScreen(hwnd, &screenPt);
			ForwardMouseMessageToWindow(m_forwardMouseTarget ? m_forwardMouseTarget : FindWindowBelowPoint(screenPt), WM_LBUTTONUP, wParam, screenPt);
			m_forwardMouseDragActive = false;
			m_forwardMouseTarget = nullptr;
			if (GetCapture() == hwnd) {
				ReleaseCapture();
			}
			return 0;
		}

		// 释放进度条拖动

		if (m_isDraggingProgress) {

			m_isDraggingProgress = false;

			m_justReleasedProgress = true;

			m_progressReleaseTime = GetTickCount64();

			// 设置播放位置

			auto duration = m_mediaMonitor.GetDuration();

			if (duration.count() > 0) {

				auto newPosition = std::chrono::seconds((long long)(m_tempProgress * duration.count()));

				m_mediaMonitor.SetPosition(newPosition);

			}

			return 0;

		}

		// 如果之前按下了按钮，松开时触发它

		if (m_pressedButtonIndex != -1) {

			const IslandDisplayMode currentMode = DetermineDisplayMode();
			int hit = m_layoutController.HitTestPlaybackButtons(pt, currentMode == IslandDisplayMode::MusicExpanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);

			// 只有松开时鼠标还在那个按钮上，才算数（防止按错滑动取消）

			if (hit == m_pressedButtonIndex) {

				if (hit == 0) m_mediaMonitor.Previous();

				else if (hit == 1) m_mediaMonitor.PlayPause();

				else if (hit == 2) m_mediaMonitor.Next();

			}

			m_pressedButtonIndex = -1; // 取消按下状态

			StartAnimation(); // 恢复图标大小

			return 0;

		}

		m_dragging = false;

		ReleaseCapture();

		return 0;

	}

	case WM_DRAG_ENTER: {
		m_isDragHovering = true;
		Invalidate(Dirty_FileDrop);
		return 0;
	}

	case WM_DRAG_LEAVE: {
		m_isDragHovering = false;
		m_fileHoveredIndex = -1;
		Invalidate(Dirty_FileDrop);
		return 0;
	}

	case WM_DROP_FILE: {
		HDROP hDrop = (HDROP)wParam;
		std::vector<std::wstring> droppedPaths;
		UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
		for (UINT i = 0; i < fileCount; ++i) {
			wchar_t path[MAX_PATH];
			DragQueryFileW(hDrop, i, path, MAX_PATH);
			droppedPaths.emplace_back(path);
		}
		DragFinish(hDrop);

		m_isDragHovering = false;
		m_fileHoveredIndex = -1;

		std::vector<std::wstring> incomingPaths;
		for (const auto& droppedPath : droppedPaths) {
			bool isSelfDrop = false;
			for (const auto& item : m_fileStash.Items()) {
				if (PathsEqualIgnoreCase(item.stagedPath, droppedPath)) {
					isSelfDrop = true;
					break;
				}
			}
			if (isSelfDrop) {
				m_fileSelfDropDetected = true;
			}
			else {
				incomingPaths.push_back(droppedPath);
			}
		}

		std::wstring errorMessage;
		if (!incomingPaths.empty()) {
			incomingPaths.resize(1);
			while (m_fileStash.Count() > 0) {
				m_fileStash.RemoveIndex(m_fileStash.Count() - 1);
			}
			if (!m_fileStash.AddPaths(incomingPaths, &errorMessage) && !errorMessage.empty()) {
				ShowFileStashLimitAlert();
			}
		}
		m_fileSecondaryExpanded = false;
		if (m_fileStash.HasItems()) {
			m_fileSelectedIndex = 0;
		} else if (!m_fileStash.HasItems()) {
			m_fileSelectedIndex = -1;
		}
		Invalidate(Dirty_FileDrop | Dirty_Region);
		return 0;
	}

	case WM_FILE_REMOVED: {
		RemoveFileStashIndex((int)wParam);
		return 0;
	}

	case WM_HOTKEY: {

		if (wParam == HOTKEY_ID) {

			if (m_state == IslandState::Collapsed) {

				// 模拟收到通知，展开岛屿

				m_state = IslandState::Expanded;
				SetActiveExpandedMode(ActiveExpandedMode::Music);

				TransitionTo(IslandDisplayMode::MusicExpanded);

				if (m_systemMonitor.GetWeatherPlugin()) {
					m_systemMonitor.GetWeatherPlugin()->RequestRefresh();
				}
			}

			// 设定自动隐藏定时器 (4000 毫秒 = 4秒后自动收缩)

			SetTimer(hwnd, m_displayTimerId, 4000, nullptr);

		}

		// Weather test: Ctrl+Alt+W cycles weather types (7 types)
		Invalidate(Dirty_SpringAnim);
		return 0;

	}

	case WM_SHOW_ALERT: {

		AlertInfo* info = (AlertInfo*)lParam;

		m_alertQueue.push(*info);

		delete info; // 防止内存泄漏

		ProcessNextAlert();

		return 0;

	}

	case WM_DPICHANGED: {

		// 显示器 DPI 发生改变 (例如窗口被拖到了另一个缩放不同的屏幕)

		m_currentDpi = HIWORD(wParam);

		m_dpiScale = m_currentDpi / 96.0f;

		// 更新画板 DPI

		m_renderer.SetDpi((float)m_currentDpi);

		// 系统建议的新窗口物理位置和大小

		RECT* prcNewWindow = (RECT*)lParam;

		int newW = prcNewWindow->right - prcNewWindow->left;

		int newH = prcNewWindow->bottom - prcNewWindow->top;

		// 设置新位置

		SetWindowPos(hwnd, nullptr, prcNewWindow->left, prcNewWindow->top, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);

		// 重置画板表面尺寸

		m_renderer.Resize(newW, newH);

		UpdateWindowRegion();

		StartAnimation();

		return 0;

	}

	case WM_TIMER: {

		if (wParam == m_timerId) {

			UpdatePhysics();

		}

		else if (wParam == m_displayTimerId) {

			// Update weather every 10 min (fixed - runs regardless of stored files)

			{

				static size_t lastWeatherUpdate = 0;

				size_t now = GetTickCount64();

				if (now - lastWeatherUpdate > 5 * 60 * 1000) {

					m_systemMonitor.UpdateWeather();

					lastWeatherUpdate = now;

				}

				m_weatherLocationText = m_systemMonitor.GetWeatherLocationText();
				m_weatherDesc = m_systemMonitor.GetWeatherDescription();

				m_weatherTemp = m_systemMonitor.GetWeatherTemperature();
}

			// 有文件或番茄会话时不收缩

			bool keepCompact = !!m_fileStash.HasItems();
			if (auto* pomodoro = m_renderer.GetPomodoroComponent()) {
				keepCompact = keepCompact || pomodoro->HasActiveSession() || pomodoro->IsExpanded();
			}
			keepCompact = keepCompact || ShouldKeepCompactOverride() || m_todoInputActive || IsExpandedMode(ActiveExpandedMode::Todo);

			if (keepCompact) {

				KillTimer(hwnd, m_displayTimerId);

				break;

			}

			if (m_state == IslandState::Expanded) {

				m_state = IslandState::Collapsed;

				TransitionTo(DetermineDisplayMode());

				// 通知结束后，即使鼠标不在岛屿上，也启动5秒定时器缩小到mini

				SetTimer(hwnd, m_displayTimerId, 5000, nullptr);

			} else if (m_state == IslandState::Collapsed && GetTargetHeight() >= Constants::Size::COMPACT_MIN_HEIGHT) {

				// 从compact缩小到mini

				TransitionTo(DetermineDisplayMode());

				KillTimer(hwnd, m_displayTimerId);

			}

			KillTimer(hwnd, m_displayTimerId);

		}

		else if (wParam == m_alertTimerId) {

			KillTimer(hwnd, m_alertTimerId);

			m_isAlertActive = false;

			// 清理内存中的图标数据

			if (!m_currentAlert.iconData.empty()) {

				m_currentAlert.iconData.clear();

			}

			// 清理文件（兼容旧代码）

			if (m_currentAlert.type == 3 && !m_currentAlert.iconPath.empty()) {

				DeleteFileW(m_currentAlert.iconPath.c_str());

			}

			ProcessNextAlert();

			// ProcessNextAlert will call TransitionTo if there's a new alert

			// If no new alert and state is Collapsed, use DetermineDisplayMode

			if (!m_isAlertActive && m_state == IslandState::Collapsed) {

				TransitionTo(DetermineDisplayMode());

			} else {

				StartAnimation();

			}

		}

		else if (wParam == m_volumeTimerId) {

			KillTimer(hwnd, m_volumeTimerId);

			m_isVolumeControlActive = false;

			// 恢复折叠状态（如果没有其他弹窗的话）

			if (m_state == IslandState::Collapsed && !m_isAlertActive) {

				TransitionTo(DetermineDisplayMode());

			} else {

				StartAnimation();

			}

		}
		else if (wParam == m_faceUnlockTimerId) {
			ClearFaceUnlockFeedback();
		}
		else if (wParam == m_fullscreenTimerId) {
			bool isNowFullscreen = IsFullscreen();
			if (isNowFullscreen != m_isFullscreen) {
				m_isFullscreen = isNowFullscreen;
				
				// 状态变化时触发更新，包括 Region 和悬浮状态
				if (m_isFullscreen) {
					// 进入全屏，隐藏岛屿：将尺寸置 0 或移动到屏幕外，或者移除 WS_EX_TOPMOST
					// 简单处理：更新 Region 为空（隐藏）或只设置透明
					UpdateWindowRegion(); // UpdateWindowRegion 内部可能需要改，或者直接 Invalidate
				} else {
					// 退出全屏，恢复岛屿
					UpdateWindowRegion();
				}
				Invalidate(Dirty_Region);
			}
		}

		return 0;

	}

	case WM_MOUSEWHEEL: {
		IslandDisplayMode currentMode = DetermineDisplayMode();
		const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (currentMode == IslandDisplayMode::Shrunk) {
			POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ForwardMouseMessageToUnderlyingWindow(WM_MOUSEWHEEL, wParam, screenPt);
			return 0;
		}
		if (currentMode == IslandDisplayMode::Idle) {
			if (auto* todo = m_renderer.GetTodoComponent(); todo && todo->IsLaunchAnimating()) {
				return 0;
			}
		}
		POINT physicalPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(hwnd, &physicalPt);
		const POINT pt = LogicalFromPhysical(physicalPt);
		const bool isCompactHeight = GetCurrentHeight() >= Constants::Size::COMPACT_MIN_HEIGHT;

		if (m_state == IslandState::Collapsed &&
			isCompactHeight &&
			IsCompactSwitchableMode(currentMode)) {
			const auto availableModes = CollectAvailableCompactModes();
			if (availableModes.size() > 1 && delta != 0) {
				size_t currentIndex = 0;
				auto it = std::find(availableModes.begin(), availableModes.end(), currentMode);
				if (it != availableModes.end()) {
					currentIndex = static_cast<size_t>(std::distance(availableModes.begin(), it));
				}

				size_t nextIndex = currentIndex;
				if (delta > 0) {
					nextIndex = (currentIndex + availableModes.size() - 1) % availableModes.size();
				} else {
					nextIndex = (currentIndex + 1) % availableModes.size();
				}

				m_hasCompactOverride = true;
				m_compactOverrideMode = availableModes[nextIndex];
				KillTimer(hwnd, m_displayTimerId);

				if (m_compactOverrideMode == IslandDisplayMode::Idle) {
					SetTargetSize(GetCompactTargetWidth(IslandDisplayMode::Idle), Constants::Size::COMPACT_HEIGHT);
					StartAnimation();
				} else {
					TransitionTo(m_compactOverrideMode);
				}

				Invalidate(Dirty_Time | Dirty_MediaState | Dirty_Region);
			}
			return 0;
		}

		if (currentMode == IslandDisplayMode::TodoExpanded && m_renderer.OnMouseWheel((float)pt.x, (float)pt.y, delta)) {
			Invalidate(Dirty_Hover | Dirty_Region);
			return 0;
		}

		if (currentMode == IslandDisplayMode::AgentExpanded && m_renderer.OnMouseWheel((float)pt.x, (float)pt.y, delta)) {
			Invalidate(Dirty_AgentSessions | Dirty_Region | Dirty_Hover);
			return 0;
		}

		// 天气展开模式：滚轮切换 逐小时 ↔ 逐日
		if (currentMode == IslandDisplayMode::WeatherExpanded) {
			m_renderer.OnMouseWheel((float)pt.x, (float)pt.y, delta);
			m_weatherViewMode = (delta > 0) ? WeatherViewMode::Hourly : WeatherViewMode::Daily;
			EnsureRenderLoopRunning();
			return 0;
		}

		// 仅在展开态且确实有音乐会话时允许调整音量；紧凑态直接忽略滚轮
		const bool allowExpandedVolumeAdjust =
			(currentMode == IslandDisplayMode::MusicExpanded) && m_mediaMonitor.HasSession();

		if (!allowExpandedVolumeAdjust) {
			return 0;
		}

		// 获取当前音量并计算新音量（每次滚动调整 2%）
		float vol = m_mediaMonitor.GetVolume();

		vol += (delta > 0) ? 0.02f : -0.02f;

		// 限制在 0.0 到 1.0 之间

		if (vol > 1.0f) vol = 1.0f;

		if (vol < 0.0f) vol = 0.0f;

		m_mediaMonitor.SetVolume(vol);

		m_currentVolume = vol;

		// 触发音量条 UI

		if (!m_isVolumeControlActive) {

			m_isVolumeControlActive = true;

			// 如果原本是收缩状态，让它展开成弹窗大小

			if (m_state == IslandState::Collapsed && !m_isAlertActive) {

				TransitionTo(IslandDisplayMode::Volume);

			} else {

				StartAnimation();

			}

		}

		// 刷新定时器：停止滚动 2 秒后自动收缩
		SetTimer(hwnd, m_volumeTimerId, 2000, nullptr);

		Invalidate(Dirty_Volume);
		return 0;

	}

	case WM_IME_CHAR:
		if (IsTodoTextMode(DetermineDisplayMode())) {
			return 0;
		}
		break;

	case WM_IME_COMPOSITION:
		if (IsTodoTextMode(DetermineDisplayMode()) &&
			m_renderer.GetTodoComponent() &&
			m_renderer.GetTodoComponent()->OnImeComposition(hwnd, lParam)) {
			PositionActiveImeWindow();
			Invalidate(Dirty_Time | Dirty_Region);
			return 0;
		}
		break;

	case WM_IME_SETCONTEXT: {
		LRESULT imeResult = 0;
		if (IsTodoTextMode(DetermineDisplayMode()) &&
			m_renderer.GetTodoComponent() &&
			m_renderer.GetTodoComponent()->OnImeSetContext(hwnd, wParam, lParam, imeResult)) {
			return imeResult;
		}
		break;
	}

	case WM_CHAR:
		if (IsTodoTextMode(DetermineDisplayMode()) &&
			m_renderer.GetTodoComponent() &&
			m_renderer.GetTodoComponent()->OnChar((wchar_t)wParam)) {
			PositionActiveImeWindow();
			Invalidate(Dirty_Time | Dirty_Region);
			return 0;
		}
		break;

	case WM_KEYDOWN:
		if (IsTodoTextMode(DetermineDisplayMode()) &&
			m_renderer.GetTodoComponent() &&
			m_renderer.GetTodoComponent()->OnKeyDown(wParam)) {
			PositionActiveImeWindow();
			Invalidate(Dirty_Time | Dirty_Region);
			return 0;
		}
		break;

	case WM_UPDATE_ALBUM_ART:

	{

		wchar_t* path = (wchar_t*)wParam;

		if (path) {

			m_renderer.LoadAlbumArt(path);

			free(path);  // 释放 _wcsdup 分配的内存

		}

		return 0;

	}

	case WM_UPDATE_ALBUM_ART_MEMORY:

	{

		ImageData* imgData = (ImageData*)wParam;

		if (imgData) {

			if (!imgData->data.empty()) {

				m_renderer.LoadAlbumArtFromMemory(imgData->data);

			}

			// 注意：这里我们不删除 imgData->data，因为 MediaMonitor 保留了所有权

			delete imgData;

		}

		return 0;

	}

	case WM_SHOW_ALERT_MEMORY:

	{

		AlertInfo* info = (AlertInfo*)lParam;

		if (info) {

			m_alertQueue.push(*info);

			// 注意：不要删除 info->iconData，因为它还在队列中使用

			delete info;

			ProcessNextAlert();

		}

		return 0;

	}

	case WM_TRAYICON:
	{
		if (lParam == WM_RBUTTONUP) {
			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING, 1, L"设置...");
			AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenu(hMenu, MF_STRING, 2, L"退出");

			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hwnd);

			int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
			DestroyMenu(hMenu);

			if (cmd == 1) {
				m_settingsWindow.Toggle();
			} else if (cmd == 2) {
				PostMessage(hwnd, WM_CLOSE, 0, 0);
			}
		}
		return 0;
	}

	case WM_POWERBROADCAST:

		m_systemMonitor.OnPowerEvent(wParam, lParam);

		return 0;

	case WM_DESTROY:

		PostQuitMessage(0);

		return 0;

	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);

}

void DynamicIsland::CreateTrayIcon() {

	memset(&m_nid, 0, sizeof(m_nid));

	m_nid.cbSize = sizeof(m_nid);

	m_nid.hWnd = m_window.GetHWND();

	m_nid.uID = 1001;               // 自定义ID

	m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

	m_nid.uCallbackMessage = WM_TRAYICON;

	m_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // 可替换为自己的图标

	wcscpy_s(m_nid.szTip, L"Dynamic Island");

	Shell_NotifyIcon(NIM_ADD, &m_nid);

}

void DynamicIsland::RemoveTrayIcon() {

	Shell_NotifyIcon(NIM_DELETE, &m_nid);

}

void DynamicIsland::UpdateWindowRegion() {
	// 计算主岛区域
	auto toPhysical = [this](float logical) {
		return static_cast<int>(std::round(logical * m_dpiScale));
	};

	float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
	float top = Constants::UI::TOP_MARGIN;
	float right = left + GetCurrentWidth();
	float bottom = top + GetCurrentHeight();
	float radius = (std::min)(Constants::UI::NOTCH_BOTTOM_RADIUS, GetCurrentHeight() / 2.0f);

	int ileft = toPhysical(left);
	int itop = toPhysical(top);
	int iright = toPhysical(right);
	int ibottom = toPhysical(bottom);
	int iradius = (std::max)(1, toPhysical(radius * 2.0f));

	HRGN hMainRgn = CreateRoundRectRgn(ileft, itop, iright, ibottom, iradius, iradius);
	if (hMainRgn) {
		HRGN hTopRectRgn = CreateRectRgn(ileft, itop, iright, toPhysical(bottom - radius));
		if (hTopRectRgn) {
			CombineRgn(hMainRgn, hMainRgn, hTopRectRgn, RGN_OR);
			DeleteObject(hTopRectRgn);
		}
	}

	// 使用动画高度更新副岛区域
	if (hMainRgn && (ShouldUseShrunkMode() || m_manualShrunk || m_shrinkAnimating)) {
		const float hudWidth = Constants::Size::COMPACT_WIDTH;
		const float hudHeight = Constants::Size::COMPACT_HEIGHT;
		const float hudLeft = (CANVAS_WIDTH - hudWidth) * 0.5f;
		const float hudTop = Constants::UI::TOP_MARGIN;
		HRGN hHudRgn = CreateRoundRectRgn(
			toPhysical(hudLeft),
			toPhysical(hudTop),
			toPhysical(hudLeft + hudWidth),
			toPhysical(hudTop + hudHeight),
			toPhysical(hudHeight),
			toPhysical(hudHeight));
		if (hHudRgn) {
			CombineRgn(hMainRgn, hMainRgn, hHudRgn, RGN_OR);
			DeleteObject(hHudRgn);
		}
	}

	float secHeight = m_layoutController.GetSecondaryHeight();
	if (secHeight > 1.0f) {
		float secWidth = Constants::Size::SECONDARY_WIDTH;
		switch (DetermineSecondaryContent()) {
		case SecondaryContentKind::FileCircle:
		case SecondaryContentKind::FileSwirlDrop:
			secWidth = Constants::Size::FILE_CIRCLE_SIZE;
			break;
		case SecondaryContentKind::FileExpanded:
			secWidth = Constants::Size::FILE_SECONDARY_EXPANDED_WIDTH;
			break;
		case SecondaryContentKind::FileDropTarget:
			secWidth = Constants::Size::FILE_SECONDARY_DROPTARGET_WIDTH;
			break;
		default:
			break;
		}
		float secLeft = (CANVAS_WIDTH - secWidth) / 2.0f;
		float secTop = bottom + 12.0f;
		if (DetermineSecondaryContent() == SecondaryContentKind::FileCircle ||
			DetermineSecondaryContent() == SecondaryContentKind::FileSwirlDrop) {
			secLeft = right + Constants::Size::FILE_CIRCLE_GAP;
			secTop = top + (GetCurrentHeight() - secHeight) * 0.5f;
			if (secTop < top + 4.0f) {
				secTop = top + 4.0f;
			}
		}
		float secRight = secLeft + secWidth;
		float secBottom = secTop + secHeight;
		float secRadius = (secHeight < 60.0f) ? (secHeight / 2.0f) : 20.0f;

		int sileft = toPhysical(secLeft);
		int sitop = toPhysical(secTop);
		int siright = toPhysical(secRight);
		int sibottom = toPhysical(secBottom);
		int siradius = toPhysical(secRadius);

		HRGN hSecRgn = CreateRoundRectRgn(sileft, sitop, siright, sibottom, siradius, siradius);
		if (hSecRgn) {
			CombineRgn(hMainRgn, hMainRgn, hSecRgn, RGN_OR);
			DeleteObject(hSecRgn);
		}
	}

	if (hMainRgn) {
		SetWindowRgn(m_window.GetHWND(), hMainRgn, TRUE);
	}
}

// 【OPT-03】处理P0紧急警告（可打断当前显示）
void DynamicIsland::ProcessAlertWithPriority(const AlertInfo& alert) {
	m_isAlertActive = true;
	m_currentAlert = alert;
	ShowWindow(m_window.GetHWND(), SW_SHOW);
	SetTimer(m_window.GetHWND(), m_alertTimerId, 3000, nullptr);
	InvalidateRect(m_window.GetHWND(), nullptr, FALSE);
}

void DynamicIsland::ProcessNextAlert() {

	if (!m_isAlertActive && !m_alertQueue.empty()) {

		m_currentAlert = m_alertQueue.front();

		m_alertQueue.pop();

		m_isAlertActive = true;

		// 如果是通知，提前让画板去加载真实的 App 图标（优先使用内存数据）

		if (m_currentAlert.type == 3) {

			if (!m_currentAlert.iconData.empty()) {

				m_renderer.LoadAlertIconFromMemory(m_currentAlert.iconData);

			}

			else if (!m_currentAlert.iconPath.empty()) {

				m_renderer.LoadAlertIcon(m_currentAlert.iconPath);

			}

		}

		if (m_state == IslandState::Collapsed) {

			TransitionTo(IslandDisplayMode::Alert);

		} else {

			StartAnimation();

		}

		SetTimer(m_window.GetHWND(), m_alertTimerId, 3000, nullptr);

	}

}

std::wstring DynamicIsland::GetCurrentTimeString() {

	SYSTEMTIME st;

	GetLocalTime(&st);

	wchar_t timeStr[64];

	swprintf_s(timeStr, 64, L"%02d:%02d", st.wHour, st.wMinute);

	return std::wstring(timeStr);

}

