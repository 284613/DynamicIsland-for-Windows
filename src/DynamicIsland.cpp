//DynamicIsland.cpp



#include "DynamicIsland.h"



#include "EventBus.h"



#include <windowsx.h>



#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")



#include <chrono>



#include "RenderEngine.h"
#include "LyricsMonitor.h"

namespace {
std::wstring GetInputDebugLogPath() {
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return L"DynamicIsland_input_debug.log";
    }
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath) + L"\\DynamicIsland_input_debug.log";
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

const wchar_t* SecondaryKindToString(SecondaryContentKind kind) {
    switch (kind) {
    case SecondaryContentKind::None: return L"None";
    case SecondaryContentKind::Volume: return L"Volume";
    case SecondaryContentKind::FileMini: return L"FileMini";
    case SecondaryContentKind::FileExpanded: return L"FileExpanded";
    case SecondaryContentKind::FileDropTarget: return L"FileDropTarget";
    default: return L"Unknown";
    }
}

const wchar_t* FileHitKindToString(FilePanelComponent::HitResult::Kind kind) {
    switch (kind) {
    case FilePanelComponent::HitResult::Kind::None: return L"None";
    case FilePanelComponent::HitResult::Kind::MiniBody: return L"MiniBody";
    case FilePanelComponent::HitResult::Kind::ExpandedBackground: return L"ExpandedBackground";
    case FilePanelComponent::HitResult::Kind::FileItem: return L"FileItem";
    default: return L"Unknown";
    }
}
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





}





DynamicIsland::DynamicIsland()



{





}





void DynamicIsland::TransitionTo(IslandDisplayMode mode) {

	switch (mode) {

		case IslandDisplayMode::Idle:

			SetTargetSize(Constants::Size::COLLAPSED_WIDTH, Constants::Size::COLLAPSED_HEIGHT);

			break;

		case IslandDisplayMode::MusicCompact:

			SetTargetSize(Constants::Size::COMPACT_WIDTH, Constants::Size::COMPACT_HEIGHT);

			break;

		case IslandDisplayMode::MusicExpanded:

			SetTargetSize(Constants::Size::EXPANDED_WIDTH, m_mediaMonitor.HasSession() ? Constants::Size::MUSIC_EXPANDED_HEIGHT : Constants::Size::EXPANDED_HEIGHT);

			break;

		case IslandDisplayMode::Alert:

			SetTargetSize(Constants::Size::ALERT_WIDTH, Constants::Size::ALERT_HEIGHT);

			break;

		case IslandDisplayMode::Volume:

			SetTargetSize(Constants::Size::ALERT_WIDTH, Constants::Size::ALERT_HEIGHT);

			break;

		case IslandDisplayMode::WeatherExpanded:
			SetTargetSize(Constants::Size::WEATHER_EXPANDED_WIDTH, Constants::Size::WEATHER_EXPANDED_HEIGHT);
			break;

		case IslandDisplayMode::FileDrop:

			SetTargetSize(Constants::Size::EXPANDED_WIDTH, Constants::Size::EXPANDED_HEIGHT);

			break;

	}

	StartAnimation();

}





void DynamicIsland::SetTargetSize(float width, float height) {

	m_layoutController.SetTargetSize(width, height);

}





IslandDisplayMode DynamicIsland::DetermineDisplayMode() const {
	// PR6: Use priority table for configurable scheduling
	for (const auto& entry : m_priorityTable) {
		if (entry.condition()) {
			return entry.mode;
		}
	}
	return IslandDisplayMode::Idle;
}

SecondaryContentKind DynamicIsland::DetermineSecondaryContent() const {
	const bool isExpandedEnough = (GetCurrentHeight() >= Constants::Size::COMPACT_MIN_HEIGHT);
	if (m_isVolumeControlActive && isExpandedEnough && !m_isAlertActive) {
		return SecondaryContentKind::Volume;
	}
	if (m_isDragHovering) {
		return SecondaryContentKind::FileDropTarget;
	}
	if (m_fileStash.HasItems()) {
		return m_fileSecondaryExpanded ? SecondaryContentKind::FileExpanded : SecondaryContentKind::FileMini;
	}
	return SecondaryContentKind::None;
}

D2D1_RECT_F DynamicIsland::GetSecondaryRectLogical() const {
	float secHeight = m_layoutController.GetSecondaryHeight();
	float secWidth = Constants::Size::SECONDARY_WIDTH;
	switch (DetermineSecondaryContent()) {
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
	float top = 10.0f;
	float bottom = top + GetCurrentHeight();
	float secLeft = (CANVAS_WIDTH - secWidth) / 2.0f;
	float secTop = bottom + 12.0f;
	return D2D1::RectF(secLeft, secTop, secLeft + secWidth, secTop + secHeight);
}

void DynamicIsland::ResetFileSecondaryInteraction() {
	m_fileHoveredIndex = -1;
	m_filePressedIndex = -1;
	m_fileLastClickIndex = -1;
	m_fileLastClickTime = 0;
	m_fileDragStarted = false;
	m_filePressPoint = {};
}

void DynamicIsland::ShowFileStashLimitAlert() {
	AlertInfo info{};
	info.type = Constants::Alert::TYPE_FILE;
	info.name = L"文件暂存已满";
	info.deviceType = L"最多暂存 5 个文件";
	info.priority = PRIORITY_P3_BACKGROUND;
	m_alertQueue.push(info);
	ProcessNextAlert();
}

void DynamicIsland::RemoveFileStashIndex(int index) {
	if (index < 0 || index >= (int)m_fileStash.Count()) return;
	m_fileStash.RemoveIndex((size_t)index);
	if (!m_fileStash.HasItems()) {
		m_fileSecondaryExpanded = false;
		m_fileSelectedIndex = -1;
		m_fileHoveredIndex = -1;
	} else if (m_fileSelectedIndex >= (int)m_fileStash.Count()) {
		m_fileSelectedIndex = (int)m_fileStash.Count() - 1;
	}
}

bool DynamicIsland::HandleFileSecondaryMouseDown(HWND hwnd, POINT pt) {
	SecondaryContentKind secondary = DetermineSecondaryContent();
	D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
	{
		std::wostringstream oss;
		oss << L"MouseDown secondary=" << SecondaryKindToString(secondary)
			<< L" pt=(" << pt.x << L"," << pt.y << L")"
			<< L" rect=(" << secondaryRect.left << L"," << secondaryRect.top << L"," << secondaryRect.right << L"," << secondaryRect.bottom << L")"
			<< L" count=" << m_fileStash.Count()
			<< L" expanded=" << (m_fileSecondaryExpanded ? 1 : 0);
		AppendInputDebugLog(oss.str());
	}
	if (secondary != SecondaryContentKind::FileMini &&
		secondary != SecondaryContentKind::FileExpanded &&
		secondary != SecondaryContentKind::FileDropTarget) {
		AppendInputDebugLog(L"MouseDown ignored: secondary content not file-related");
		return false;
	}

	if ((float)pt.x < secondaryRect.left || (float)pt.x > secondaryRect.right ||
		(float)pt.y < secondaryRect.top || (float)pt.y > secondaryRect.bottom) {
		AppendInputDebugLog(L"MouseDown ignored: point outside secondary rect");
		return false;
	}

	auto hit = m_renderer.HitTestFileSecondary((float)pt.x, (float)pt.y);
	{
		std::wostringstream oss;
		oss << L"MouseDown hit=" << FileHitKindToString(hit.kind) << L" index=" << hit.index;
		AppendInputDebugLog(oss.str());
	}
	if (secondary == SecondaryContentKind::FileMini || secondary == SecondaryContentKind::FileDropTarget) {
		m_fileSecondaryExpanded = m_fileStash.HasItems();
		StartAnimation();
		AppendInputDebugLog(m_fileSecondaryExpanded ? L"MouseDown action: expand file secondary" : L"MouseDown action: cannot expand because stash empty");
		Invalidate(Dirty_FileDrop | Dirty_Region);
		return true;
	}

	if (hit.kind == FilePanelComponent::HitResult::Kind::ExpandedBackground) {
		m_fileSecondaryExpanded = false;
		m_fileHoveredIndex = -1;
		StartAnimation();
		AppendInputDebugLog(L"MouseDown action: collapse file secondary");
		Invalidate(Dirty_FileDrop | Dirty_Region);
		return true;
	}

	if (hit.kind == FilePanelComponent::HitResult::Kind::FileItem) {
		m_filePressedIndex = hit.index;
		m_filePressPoint = pt;
		m_fileDragStarted = false;
		AppendInputDebugLog(L"MouseDown action: press file item");
		return true;
	}

	AppendInputDebugLog(L"MouseDown action: consumed with no state change");
	return true;
}

bool DynamicIsland::HandleFileSecondaryMouseMove(HWND hwnd, POINT pt, WPARAM keyState) {
	SecondaryContentKind secondary = DetermineSecondaryContent();
	bool secondaryVisible = (secondary == SecondaryContentKind::FileMini ||
		secondary == SecondaryContentKind::FileExpanded ||
		secondary == SecondaryContentKind::FileDropTarget);
	D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
	bool insideSecondary = secondaryVisible &&
		(float)pt.x >= secondaryRect.left && (float)pt.x <= secondaryRect.right &&
		(float)pt.y >= secondaryRect.top && (float)pt.y <= secondaryRect.bottom;

	if (insideSecondary) {
		auto hit = m_renderer.HitTestFileSecondary((float)pt.x, (float)pt.y);
		int newHovered = (hit.kind == FilePanelComponent::HitResult::Kind::FileItem) ? hit.index : -1;
		if (newHovered != m_fileHoveredIndex) {
			std::wostringstream oss;
			oss << L"MouseMove hover change: secondary=" << SecondaryKindToString(secondary)
				<< L" hit=" << FileHitKindToString(hit.kind) << L" index=" << hit.index;
			AppendInputDebugLog(oss.str());
			m_fileHoveredIndex = newHovered;
			Invalidate(Dirty_Hover | Dirty_FileDrop);
		}
	}
	else if (m_fileHoveredIndex != -1) {
		m_fileHoveredIndex = -1;
		Invalidate(Dirty_Hover | Dirty_FileDrop);
	}

	if (m_filePressedIndex != -1 && (keyState & MK_LBUTTON)) {
		int dx = pt.x - m_filePressPoint.x;
		int dy = pt.y - m_filePressPoint.y;
		if (std::abs(dx) >= GetSystemMetrics(SM_CXDRAG) || std::abs(dy) >= GetSystemMetrics(SM_CYDRAG)) {
			bool moved = false;
			m_fileDragStarted = true;
			AppendInputDebugLog(L"MouseMove action: begin file drag");
			m_fileStash.BeginMoveDrag(hwnd, (size_t)m_filePressedIndex, moved);
			if (moved) {
				AppendInputDebugLog(L"MouseMove action: drag completed with move effect");
				RemoveFileStashIndex(m_filePressedIndex);
			}
			m_filePressedIndex = -1;
			Invalidate(Dirty_FileDrop | Dirty_Region);
			return true;
		}
	}

	return insideSecondary;
}

bool DynamicIsland::HandleFileSecondaryMouseUp(POINT pt) {
	SecondaryContentKind secondary = DetermineSecondaryContent();
	bool secondaryVisible = (secondary == SecondaryContentKind::FileMini ||
		secondary == SecondaryContentKind::FileExpanded ||
		secondary == SecondaryContentKind::FileDropTarget);
	D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
	bool isOverSecondary = secondaryVisible &&
		(float)pt.x >= secondaryRect.left && (float)pt.x <= secondaryRect.right &&
		(float)pt.y >= secondaryRect.top && (float)pt.y <= secondaryRect.bottom;
	auto hit = isOverSecondary ? m_renderer.HitTestFileSecondary((float)pt.x, (float)pt.y) : FilePanelComponent::HitResult{};
	{
		std::wostringstream oss;
		oss << L"MouseUp secondary=" << SecondaryKindToString(secondary)
			<< L" pt=(" << pt.x << L"," << pt.y << L")"
			<< L" inside=" << (isOverSecondary ? 1 : 0)
			<< L" hit=" << FileHitKindToString(hit.kind) << L" index=" << hit.index
			<< L" pressedIndex=" << m_filePressedIndex
			<< L" dragStarted=" << (m_fileDragStarted ? 1 : 0);
		AppendInputDebugLog(oss.str());
	}

	if (m_filePressedIndex != -1 && !m_fileDragStarted &&
		hit.kind == FilePanelComponent::HitResult::Kind::FileItem &&
		hit.index == m_filePressedIndex) {
		m_fileSelectedIndex = hit.index;
		ULONGLONG now = GetTickCount64();
		bool isDoubleClick = (m_fileLastClickIndex == hit.index) &&
			(now - m_fileLastClickTime <= (ULONGLONG)GetDoubleClickTime());
		if (isDoubleClick) {
			m_fileStash.OpenIndex((size_t)hit.index);
			m_fileLastClickIndex = -1;
			m_fileLastClickTime = 0;
			AppendInputDebugLog(L"MouseUp action: open file item");
		}
		else {
			m_fileStash.PreviewIndex((size_t)hit.index);
			m_fileLastClickIndex = hit.index;
			m_fileLastClickTime = now;
			AppendInputDebugLog(L"MouseUp action: preview file item");
		}
		Invalidate(Dirty_FileDrop);
		m_filePressedIndex = -1;
		return true;
	}

	m_filePressedIndex = -1;
	m_fileDragStarted = false;
	AppendInputDebugLog(isOverSecondary ? L"MouseUp action: consume secondary click" : L"MouseUp ignored outside secondary");
	return isOverSecondary;
}





void DynamicIsland::LoadConfig() {





	wchar_t exePath[MAX_PATH];





	GetModuleFileNameW(NULL, exePath, MAX_PATH);













	// 获取当前 exe 所在的目录





	std::wstring pathStr(exePath);





	size_t pos = pathStr.find_last_of(L"\\/");





	std::wstring configPath = pathStr.substr(0, pos) + L"\\config.ini";













	// 读取 INI 文件（如果文件不存在则使用后面的默认值）





	CANVAS_WIDTH = (float)GetPrivateProfileIntW(L"Settings", L"CanvasWidth", (int)Constants::Size::CANVAS_WIDTH, configPath.c_str());





	CANVAS_HEIGHT = (float)GetPrivateProfileIntW(L"Settings", L"CanvasHeight", (int)Constants::Size::CANVAS_HEIGHT, configPath.c_str());
	if (CANVAS_HEIGHT < Constants::Size::CANVAS_HEIGHT) {
		CANVAS_HEIGHT = Constants::Size::CANVAS_HEIGHT;
	}









	// COLLAPSED_WIDTH = (float)GetPrivateProfileIntW(L"Settings", L"CollapsedWidth", (int)Constants::Size::COLLAPSED_WIDTH, configPath.c_str());

	// COLLAPSED_HEIGHT = (float)GetPrivateProfileIntW(L"Settings", L"CollapsedHeight", (int)Constants::Size::COLLAPSED_HEIGHT, configPath.c_str());

	// COMPACT_WIDTH = (float)GetPrivateProfileIntW(L"Settings", L"CompactWidth", (int)Constants::Size::COMPACT_WIDTH, configPath.c_str());

	// COMPACT_HEIGHT = (float)GetPrivateProfileIntW(L"Settings", L"CompactHeight", (int)Constants::Size::COMPACT_HEIGHT, configPath.c_str());

	// EXPANDED_WIDTH = (float)GetPrivateProfileIntW(L"Settings", L"ExpandedWidth", (int)Constants::Size::EXPANDED_WIDTH, configPath.c_str());

	// EXPANDED_HEIGHT = (float)GetPrivateProfileIntW(L"Settings", L"ExpandedHeight", (int)Constants::Size::EXPANDED_HEIGHT, configPath.c_str());

	// MUSIC_EXPANDED_HEIGHT = (float)GetPrivateProfileIntW(L"Settings", L"MusicExpandedHeight", (int)Constants::Size::MUSIC_EXPANDED_HEIGHT, configPath.c_str());

	// ALERT_WIDTH = (float)GetPrivateProfileIntW(L"Settings", L"AlertWidth", (int)Constants::Size::ALERT_WIDTH, configPath.c_str());

	// ALERT_HEIGHT = (float)GetPrivateProfileIntW(L"Settings", L"AlertHeight", (int)Constants::Size::ALERT_HEIGHT, configPath.c_str());





	wchar_t allowedAppsBuf[512] = { 0 };





	GetPrivateProfileStringW(L"Settings", L"AllowedApps", L"微信,QQ", allowedAppsBuf, 512, configPath.c_str());





	std::wstring appsStr(allowedAppsBuf);





	size_t start = 0, end;





	while ((end = appsStr.find(L',', start)) != std::wstring::npos) {





		m_allowedApps.push_back(appsStr.substr(start, end - start));





		start = end + 1;





	}





	m_allowedApps.push_back(appsStr.substr(start));





	// 初始状态设定为折叠





	m_layoutController.SetTargetSize(Constants::Size::COLLAPSED_WIDTH, Constants::Size::COLLAPSED_HEIGHT);





}





bool DynamicIsland::Initialize(HINSTANCE hInstance) {





	LoadConfig(); // 【新增】最优先加载配置文件

	// PR6: 初始化显示模式优先级调度表
	m_priorityTable = {
		{ IslandDisplayMode::Alert,          100, [this]() { return m_isAlertActive; } },
		{ IslandDisplayMode::WeatherExpanded,  90, [this]() { return m_isWeatherExpanded; } },
		{ IslandDisplayMode::MusicExpanded,    70, [this]() { return m_state == IslandState::Expanded && m_mediaMonitor.HasSession(); } },
		{ IslandDisplayMode::Volume,           60, [this]() { return m_isVolumeControlActive; } },
		{ IslandDisplayMode::MusicCompact,     50, [this]() { return m_state == IslandState::Collapsed && m_mediaMonitor.IsPlaying(); } },
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





	// 初始化文件面板窗口`r`nm_mediaMonitor.SetTargetWindow(m_window.GetHWND());





	m_mediaMonitor.Initialize();





	m_connectionMonitor.Initialize(m_window.GetHWND());





	m_notificationMonitor.Initialize(m_window.GetHWND(), m_allowedApps);





	m_lyricsMonitor.Initialize(m_window.GetHWND());





	m_systemMonitor.Initialize(m_window.GetHWND()); // 【新增】启动电量监控





	// 【新增】立即获取天气（不等10分钟定时器）





	m_systemMonitor.UpdateWeather();





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
	m_renderer.SetLyricData({ L"", -1, -1, 0 });
	m_renderer.SetWaveformState(m_mediaMonitor.GetAudioLevel(), GetCurrentHeight());
	m_renderer.SetTimeData(false, L"");
	m_renderer.SetVolumeState(false, 0.0f);
	m_renderer.SetFileState(SecondaryContentKind::None, m_fileStash.Items(), -1, -1);
	m_renderer.SetWeatherState(m_weatherDesc, m_weatherTemp, L"", {}, {}, false, m_weatherViewMode);
	m_renderer.SetAlertState(false, AlertInfo{});





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
    if (!m_layoutController.IsSettled()) return true;       // 弹簧动画中
    if (m_cachedIsPlaying && m_state != IslandState::Alert) return true;  // 音频可视化
    if (m_renderer.HasActiveAnimations()) return true;      // 组件动画/滚动中
    if (m_isVolumeControlActive) return true;               // 音量条显示中
    if (m_isAlertActive) return true;                       // 通知显示中
    if (m_isDraggingProgress) return true;                  // 拖动进度条
    if (m_isDragHovering) return true;                      // 文件拖拽悬停
    if (m_isWeatherExpanded) return true;                   // 天气展开面板动画持续运行
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





	// 正常物理更新 - 使用弹簧动画系统
	SecondaryContentKind secondaryContent = DetermineSecondaryContent();
	switch (secondaryContent) {
	case SecondaryContentKind::Volume:
	case SecondaryContentKind::FileMini:
		m_layoutController.SetSecondaryTarget(Constants::Size::SECONDARY_HEIGHT, 1.0f);
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





	// 物理动画完成后，根据状态自动调整目标尺寸





	if (m_layoutController.IsAnimating() && m_state == IslandState::Collapsed) {





		// 如果正在播放且当前是Mini态或目标是Mini，自动展开到Compact





		if (m_mediaMonitor.IsPlaying() && GetTargetHeight() < Constants::Size::COMPACT_MIN_HEIGHT && !m_fileStash.HasItems()) {





			SetTargetSize(Constants::Size::COMPACT_WIDTH, Constants::Size::COMPACT_HEIGHT);





			StartAnimation();





		}

	}










	std::wstring realTitle = m_cachedTitle;
	std::wstring realArtist = m_cachedArtist;





	// 检查是否显示时间：收缩状态 + 无通知 + 不在播放（暂停就显示时间）





	bool showTime = (m_state == IslandState::Collapsed && !m_isAlertActive && !isPlaying);





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

	if (nowMs - lastProgressPoll > 100) {
		lastProgressPoll = nowMs;
		cachedProgress = (duration.count() > 0)
			? static_cast<float>(position.count()) / duration.count()
			: 0.0f;
		if (hasSession) {
			int64_t positionMs = position.count() * 1000;
			auto monData = m_lyricsMonitor.GetLyricData(positionMs);
			cachedLyricData = { monData.text, monData.currentMs, monData.nextMs, monData.positionMs };
		}
	}

	float progress = cachedProgress;
	LyricData currentLyricData = cachedLyricData;

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
	m_renderer.SetMusicInteractionState(m_hoveredButtonIndex, m_pressedButtonIndex, m_hoveredProgress, m_pressedProgress);
	m_renderer.SetLyricData(currentLyricData);
	m_renderer.SetWaveformState(realAudioLevel, GetCurrentHeight());
	m_renderer.SetTimeData(showTime, timeStr);
	m_renderer.SetVolumeState(m_isVolumeControlActive, m_currentVolume);
	m_renderer.SetFileState(secondaryContent, m_fileStash.Items(), m_fileSelectedIndex, m_fileHoveredIndex);
	m_renderer.SetAlertState(m_isAlertActive, m_currentAlert);

	std::vector<HourlyForecast> hourlyForecasts;
	std::vector<DailyForecast> dailyForecasts;
	std::wstring weatherIconId = L"100";
	if (m_systemMonitor.GetWeatherPlugin()) {
		m_weatherDesc = m_systemMonitor.GetWeatherPlugin()->GetWeatherDescription();
		m_weatherTemp = m_systemMonitor.GetWeatherPlugin()->GetTemperature();
		weatherIconId = m_systemMonitor.GetWeatherPlugin()->GetIconId();
		hourlyForecasts = m_systemMonitor.GetWeatherPlugin()->GetHourlyForecast();
		dailyForecasts = m_systemMonitor.GetWeatherPlugin()->GetDailyForecast();
	}
	m_renderer.SetWeatherState(m_weatherDesc, m_weatherTemp, weatherIconId,
		hourlyForecasts, dailyForecasts, m_isWeatherExpanded, m_weatherViewMode);

	IslandDisplayMode mode = DetermineDisplayMode();
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





	case WM_NCHITTEST: {
		POINT physicalPt;
		physicalPt.x = GET_X_LPARAM(lParam);
		physicalPt.y = GET_Y_LPARAM(lParam);
		ScreenToClient(hwnd, &physicalPt);

		POINT pt;
		pt.x = (LONG)std::round(physicalPt.x / m_dpiScale);
		pt.y = (LONG)std::round(physicalPt.y / m_dpiScale);

		float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
		float right = left + GetCurrentWidth();
		float top = 10.0f;
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





		POINT pt; // 转成逻辑坐标，供后续的 HitTest 使用





		// 【修改】加入 std::round，解决边缘像素计算错位的问题





		pt.x = (LONG)std::round(physicalPt.x / m_dpiScale);





		pt.y = (LONG)std::round(physicalPt.y / m_dpiScale);





		// 鼠标悬停检测





		float hoverLeft = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;





		float hoverRight = hoverLeft + GetCurrentWidth();





		float hoverTop = 10.0f;





		float hoverBottom = hoverTop + GetCurrentHeight();





		bool isOverIsland = (pt.x >= hoverLeft && pt.x <= hoverRight && pt.y >= hoverTop && pt.y <= hoverBottom);









		if (isOverIsland != m_isHovering) {





			m_isHovering = isOverIsland;





			// 悬停时：停止待处理的自动收缩定时器





			if (m_isHovering) {

				KillTimer(m_window.GetHWND(), m_displayTimerId);

			}





			bool suppressHoverResize = m_fileStash.HasItems();

			





			if (!suppressHoverResize && m_isHovering && m_state == IslandState::Collapsed) {





				if (GetTargetHeight() < Constants::Size::COMPACT_MIN_HEIGHT) {





					SetTargetSize(Constants::Size::COMPACT_WIDTH, Constants::Size::COMPACT_HEIGHT);





					StartAnimation();





				}





			} else if (!suppressHoverResize && !m_isHovering && m_state == IslandState::Collapsed) {





				// 鼠标离开时：如果是compact大小，启动5秒定时器后缩小到mini





				if (GetTargetHeight() >= Constants::Size::COMPACT_MIN_HEIGHT) {

					// 先启动定时器，5秒后会缩小

					SetTimer(m_window.GetHWND(), m_displayTimerId, 5000, nullptr);

				}





			}





		}





		// Weather icon hover detection: trigger animation on first hover
		if (m_state == IslandState::Collapsed && !m_mediaMonitor.IsPlaying() && !m_isWeatherExpanded) {
			float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
			float right = left + GetCurrentWidth();
			float top = 10.0f;
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

// 进度条拖动





		if (m_isDraggingProgress) {





			// 计算拖动位置对应的进度 (使用与RenderEngine一致的坐标)





			float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;





			float top = 10.0f;





			float right = left + GetCurrentWidth();





			float artSize = 60.0f;





			float artLeft = left + 20.0f;





			float textLeft = artLeft + artSize + 15.0f;





			float titleMaxWidth = (right - 20.0f) - textLeft;





			float progressBarLeft = textLeft - 80.0f;





			float progressBarRight = textLeft + titleMaxWidth;





			float clickX = (float)pt.x;





			float progress = (clickX - progressBarLeft) / (progressBarRight - progressBarLeft);





			progress = (progress < 0.0f) ? 0.0f : ((progress > 1.0f) ? 1.0f : progress);





			// 只更新临时进度，不设置播放位置





			m_tempProgress = progress;





			StartAnimation();





			return 0;





		}





		if (m_dragging) {





			POINT current;





			GetCursorPos(&current);





			int dx = current.x - m_dragStart.x;





			int dy = current.y - m_dragStart.y;





			// 计算新窗口位置





			int newLeft = m_windowStart.x + dx;





			int newTop = m_windowStart.y + dy;





			// 获取当前窗口所在显示器的工作区





			HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);





			MONITORINFO mi = { sizeof(MONITORINFO) };





			if (GetMonitorInfo(hMonitor, &mi)) {





				RECT workArea = mi.rcWork; // 工作区（排除任务栏）





				// 获取窗口大小





				RECT windowRect;





				GetWindowRect(hwnd, &windowRect);





				int width = windowRect.right - windowRect.left;





				int height = windowRect.bottom - windowRect.top;





				// 限制新位置不超出工作区





				if (newLeft < workArea.left) newLeft = workArea.left;





				if (newTop < workArea.top) newTop = workArea.top;





				if (newLeft + width > workArea.right) newLeft = workArea.right - width;





				if (newTop + height > workArea.bottom) newTop = workArea.bottom - height;





			}





			SetWindowPos(hwnd, nullptr, newLeft, newTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOREDRAW);





			return 0;





		}





		int hit = m_layoutController.HitTestPlaybackButtons(pt, m_state == IslandState::Expanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);





		if (hit != m_hoveredButtonIndex) {





			m_hoveredButtonIndex = hit;





			StartAnimation(); // 叫醒渲染器画高亮





		}





		// 进度条悬停检测





		int progressHit = m_layoutController.HitTestProgressBar(pt, m_state == IslandState::Expanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);





		if (progressHit != m_hoveredProgress) {





			m_hoveredProgress = progressHit;





			StartAnimation();





		}





		TRACKMOUSEEVENT tme{};





		tme.cbSize = sizeof(TRACKMOUSEEVENT);





		tme.dwFlags = TME_LEAVE;





		tme.hwndTrack = hwnd;





		TrackMouseEvent(&tme);





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
			if (m_state == IslandState::Collapsed &&
			    GetTargetHeight() >= Constants::Size::COMPACT_MIN_HEIGHT) {
			    SetTimer(hwnd, m_displayTimerId, 5000, nullptr);
			}}

		if (m_fileHoveredIndex != -1) {
			m_fileHoveredIndex = -1;
		}
		Invalidate(Dirty_Hover | Dirty_FileDrop);
		return 0;





	}





	// 鼠标点击胶囊体





	case WM_LBUTTONDOWN: {





		POINT physicalPt;





		physicalPt.x = GET_X_LPARAM(lParam);





		physicalPt.y = GET_Y_LPARAM(lParam);





		POINT pt; // 转成逻辑坐标，供后续的 HitTest 使用





		// 【修改】加入 std::round，解决边缘像素计算错位的问题





		pt.x = (LONG)std::round(physicalPt.x / m_dpiScale);





		pt.y = (LONG)std::round(physicalPt.y / m_dpiScale);

		if (HandleFileSecondaryMouseDown(hwnd, pt)) {
			return 0;
		}

		SecondaryContentKind secondary = DetermineSecondaryContent();
		IslandDisplayMode primaryMode = DetermineDisplayMode();
		float islandLeft = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
		float islandRight = islandLeft + GetCurrentWidth();
		float islandTop = 10.0f;
		float islandBottom = islandTop + GetCurrentHeight();
		bool isOverPrimaryIsland = pt.x >= islandLeft && pt.x <= islandRight && pt.y >= islandTop && pt.y <= islandBottom;
		if (secondary == SecondaryContentKind::FileMini && primaryMode == IslandDisplayMode::Idle && isOverPrimaryIsland) {
			m_fileSecondaryExpanded = true;
			StartAnimation();
			Invalidate(Dirty_FileDrop | Dirty_Region);
			AppendInputDebugLog(L"MouseDown fallback: expand file secondary from primary island click");
			return 0;
		}

		// --- 检查是否点中了按钮 ---





		int hit = m_layoutController.HitTestPlaybackButtons(pt, m_state == IslandState::Expanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);





		if (hit != -1) {





			m_pressedButtonIndex = hit;





			StartAnimation(); // 触发缩小（按下）动画





			return 0; // 拦截点击，不要触发窗口拖拽！





		}





		// 点击胶囊不再打开文件 - 文件在侧边栏管理





		// --- 检查是否点中了进度条 ---





		int progressHit = m_layoutController.HitTestProgressBar(pt, m_state == IslandState::Expanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);





		if (progressHit != -1) {





			m_isDraggingProgress = true;





			// 计算点击位置对应的进度 (使用与RenderEngine一致的坐标)





			float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;





			float top = 10.0f;





			float right = left + GetCurrentWidth();





			float artSize = 60.0f;





			float artLeft = left + 20.0f;





			float textLeft = artLeft + artSize + 15.0f;





			float titleMaxWidth = (right - 20.0f) - textLeft;





			float progressBarLeft = textLeft - 80.0f;





			float progressBarRight = textLeft + titleMaxWidth;





			float clickX = (float)pt.x;





			float progress = (clickX - progressBarLeft) / (progressBarRight - progressBarLeft);





			progress = (progress < 0.0f) ? 0.0f : ((progress > 1.0f) ? 1.0f : progress);





			// 初始化临时进度





			m_tempProgress = progress;





			StartAnimation();





			return 0;





		}

		// --- 检查是否点中了天气图标 ---
		if (m_state == IslandState::Collapsed && !m_mediaMonitor.IsPlaying() && !m_isWeatherExpanded) {
			float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
			float right = left + GetCurrentWidth();
			float top = 10.0f;
			float islandHeight = GetCurrentHeight();
			float iconSize = islandHeight * 0.4f;
			float iconX = right - iconSize - 15.0f;
			float iconY = top + (islandHeight - iconSize) / 2.0f;

			// 扩大点击区域，包含图标和温度文字
			bool isOverWeather = (pt.x >= iconX - 40.0f && pt.x <= iconX + iconSize &&
			                      pt.y >= top && pt.y <= top + islandHeight);

			if (isOverWeather) {
				m_isWeatherExpanded = true;
				m_state = IslandState::Expanded; // 必须将状态置为Expanded，这样收缩时才能正确处理
				TransitionTo(IslandDisplayMode::WeatherExpanded);
				return 0;
			}
		}

		// 如果没点中按钮，执行原有的拖动和折叠逻辑





		m_dragging = true;





		GetCursorPos(&m_dragStart);





		RECT rect;





		GetWindowRect(hwnd, &rect);





		m_windowStart.x = rect.left;





		m_windowStart.y = rect.top;





		SetCapture(hwnd);





		if (m_state == IslandState::Collapsed) {
			// 仅在当前确实处于音乐紧凑态时，点击主岛才展开到音乐面板
			if (DetermineDisplayMode() == IslandDisplayMode::MusicCompact) {
				m_state = IslandState::Expanded;
				TransitionTo(IslandDisplayMode::MusicExpanded);
				m_mediaMonitor.SetExpandedState(true);
				m_mediaMonitor.RequestAlbumArtRefresh();

				if (m_systemMonitor.GetWeatherPlugin()) {
					m_systemMonitor.GetWeatherPlugin()->RequestRefresh();
				}
			}

		} else {

			// 从 Expanded 状态点击 → Compact（不经过 Mini）

			m_state = IslandState::Collapsed;
			m_isWeatherExpanded = false;

			// 【修复】点击收缩时，立即关闭副岛音量显示，防止收缩后主岛变成音量条
			if (m_isVolumeControlActive) {
				m_isVolumeControlActive = false;
				KillTimer(hwnd, m_volumeTimerId);
			}
			m_mediaMonitor.SetExpandedState(false);

			IslandDisplayMode nextMode = DetermineDisplayMode();
			if (nextMode == IslandDisplayMode::Idle) {
				SetTargetSize(Constants::Size::COMPACT_WIDTH, Constants::Size::COMPACT_HEIGHT);
				StartAnimation();
				SetTimer(hwnd, m_displayTimerId, 5000, nullptr);
			} else {
				TransitionTo(nextMode);
			}

		}





		return 0;





	}





	case WM_LBUTTONUP:





	{





		POINT physicalPt;





		physicalPt.x = GET_X_LPARAM(lParam);





		physicalPt.y = GET_Y_LPARAM(lParam);





		POINT pt; // 转成逻辑坐标，供后续的 HitTest 使用





		// 【修改】加入 std::round，解决边缘像素计算错位的问题





		pt.x = (LONG)std::round(physicalPt.x / m_dpiScale);





		pt.y = (LONG)std::round(physicalPt.y / m_dpiScale);

		if (HandleFileSecondaryMouseUp(pt)) {
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





			int hit = m_layoutController.HitTestPlaybackButtons(pt, m_state == IslandState::Expanded, m_mediaMonitor.HasSession(), CANVAS_WIDTH, GetCurrentWidth(), GetCurrentHeight(), m_dpiScale);





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
		std::wstring errorMessage;
		if (!m_fileStash.AddPaths(droppedPaths, &errorMessage) && !errorMessage.empty()) {
			ShowFileStashLimitAlert();
		}
		if (m_fileStash.HasItems() && m_fileSelectedIndex < 0) {
			m_fileSelectedIndex = 0;
		}
		Invalidate(Dirty_FileDrop);
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





				m_weatherDesc = m_systemMonitor.GetWeatherDescription();





				m_weatherTemp = m_systemMonitor.GetWeatherTemperature();
}





			// 有文件时不收缩





			if (!!m_fileStash.HasItems()) {





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

				SetTargetSize(Constants::Size::COLLAPSED_WIDTH, Constants::Size::COLLAPSED_HEIGHT);

				StartAnimation();

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

		// 天气展开模式：滚轮切换 逐小时 ↔ 逐日
		if (currentMode == IslandDisplayMode::WeatherExpanded) {
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			m_renderer.OnMouseWheel(0.0f, 0.0f, delta);
			m_weatherViewMode = (delta > 0) ? WeatherViewMode::Hourly : WeatherViewMode::Daily;
			EnsureRenderLoopRunning();
			return 0;
		}

		// 【修改】仅在音乐模式（展开/紧凑）且确实有音乐会话时允许调整音量
		bool hasMusic = (currentMode == IslandDisplayMode::MusicCompact ||
		                 (currentMode == IslandDisplayMode::MusicExpanded && m_mediaMonitor.HasSession()));

		if (!hasMusic && !m_isVolumeControlActive) {
			return 0;
		}

		// 获取滚轮滚动的方向和大小 (+120 向上, -120 向下)
		int delta = GET_WHEEL_DELTA_WPARAM(wParam);

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





		if (lParam == WM_RBUTTONUP) // 右键点击弹出菜单





		{





			HMENU hMenu = CreatePopupMenu();





			AppendMenu(hMenu, MF_STRING, 1, L"退出");





			POINT pt;





			GetCursorPos(&pt);





			SetForegroundWindow(hwnd); // 重要，让菜单正确显示





			int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);





			DestroyMenu(hMenu);





			if (cmd == 1) {





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
	float left = (CANVAS_WIDTH - GetCurrentWidth()) / 2.0f;
	float top = 10.0f;
	float right = left + GetCurrentWidth();
	float bottom = top + GetCurrentHeight();
	float radius = (GetCurrentHeight() < 60.0f) ? (GetCurrentHeight() / 2.0f) : 20.0f;

	int ileft = (int)(left * m_dpiScale + 0.5f);
	int itop = (int)(top * m_dpiScale + 0.5f);
	int iright = (int)(right * m_dpiScale + 0.5f);
	int ibottom = (int)(bottom * m_dpiScale + 0.5f);
	int iradius = (int)(radius * m_dpiScale + 0.5f);

	HRGN hMainRgn = CreateRoundRectRgn(ileft, itop, iright, ibottom, iradius, iradius);

	// 使用动画高度更新副岛区域
	float secHeight = m_layoutController.GetSecondaryHeight();
	if (secHeight > 1.0f) {
		float secWidth = Constants::Size::SECONDARY_WIDTH;
		switch (DetermineSecondaryContent()) {
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
		float secRight = secLeft + secWidth;
		float secBottom = secTop + secHeight;
		float secRadius = (secHeight < 60.0f) ? (secHeight / 2.0f) : 20.0f;

		int sileft = (int)(secLeft * m_dpiScale + 0.5f);
		int sitop = (int)(secTop * m_dpiScale + 0.5f);
		int siright = (int)(secRight * m_dpiScale + 0.5f);
		int sibottom = (int)(secBottom * m_dpiScale + 0.5f);
		int siradius = (int)(secRadius * m_dpiScale + 0.5f);

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
