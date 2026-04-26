#include "RenderEngine.h"
#include "Constants.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {
Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateNotchGeometry(
    ID2D1Factory1* factory,
    const D2D1_RECT_F& rect,
    float bottomRadius)
{
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (!factory) {
        return geometry;
    }

    const float radius = (std::max)(0.0f, (std::min)(bottomRadius, (rect.bottom - rect.top) / 2.0f));
    if (FAILED(factory->CreatePathGeometry(&geometry))) {
        geometry.Reset();
        return geometry;
    }

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink))) {
        geometry.Reset();
        return geometry;
    }

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(rect.left, rect.top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(rect.right, rect.top));

    if (radius > 0.01f) {
        sink->AddLine(D2D1::Point2F(rect.right, rect.bottom - radius));
        sink->AddArc(D2D1::ArcSegment(
            D2D1::Point2F(rect.right - radius, rect.bottom),
            D2D1::SizeF(radius, radius),
            0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(rect.left + radius, rect.bottom));
        sink->AddArc(D2D1::ArcSegment(
            D2D1::Point2F(rect.left, rect.bottom - radius),
            D2D1::SizeF(radius, radius),
            0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            D2D1_ARC_SIZE_SMALL));
    } else {
        sink->AddLine(D2D1::Point2F(rect.right, rect.bottom));
        sink->AddLine(D2D1::Point2F(rect.left, rect.bottom));
    }

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        geometry.Reset();
    }

    return geometry;
}

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

float SmoothStep(float value) {
    value = Clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

D2D1_RECT_F LerpRect(const D2D1_RECT_F& a, const D2D1_RECT_F& b, float t) {
    return D2D1::RectF(
        Lerp(a.left, b.left, t),
        Lerp(a.top, b.top, t),
        Lerp(a.right, b.right, t),
        Lerp(a.bottom, b.bottom, t));
}

float MusicExpansionProgress(float height) {
    return SmoothStep((height - Constants::Size::MUSIC_COMPACT_HEIGHT) /
        ((std::max)(1.0f, Constants::Size::MUSIC_EXPANDED_HEIGHT - Constants::Size::MUSIC_COMPACT_HEIGHT)));
}

D2D1_RECT_F BuildMusicLyricRect(const D2D1_RECT_F& rect, float height) {
    const float progress = MusicExpansionProgress(height);
    const D2D1_RECT_F compactRect = D2D1::RectF(
        rect.left + 64.0f,
        rect.top + 40.0f,
        rect.right - 58.0f,
        rect.bottom - 6.0f);
    const D2D1_RECT_F expandedRect = D2D1::RectF(
        rect.left + 95.0f,
        rect.top + 66.0f,
        rect.right - 24.0f,
        rect.top + 90.0f);
    return LerpRect(compactRect, expandedRect, progress);
}
}

RenderEngine::RenderEngine() = default;
RenderEngine::~RenderEngine() = default;

bool RenderEngine::Initialize(HWND hwnd, int canvasWidth, int canvasHeight) {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &m_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) return false;

    hr = m_d3dDevice.As(&m_dxgiDevice);
    if (FAILED(hr)) return false;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = m_d2dFactory->CreateDevice(m_dxgiDevice.Get(), &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) return false;

    hr = DCompositionCreateDevice(m_dxgiDevice.Get(), __uuidof(IDCompositionDevice), reinterpret_cast<void**>(m_dcompDevice.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &m_dcompTarget);
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateVisual(&m_rootVisual);
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateSurface(canvasWidth, canvasHeight, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED, &m_surface);
    if (FAILED(hr)) return false;

    m_rootVisual->SetContent(m_surface.Get());
    m_dcompTarget->SetRoot(m_rootVisual.Get());

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory);
    if (FAILED(hr)) return false;

    hr = m_dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-cn", &m_textFormatTitle);
    if (FAILED(hr)) return false;

    hr = m_dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-cn", &m_textFormatSub);
    if (FAILED(hr)) return false;

    hr = m_dwriteFactory->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"zh-cn", &m_iconTextFormat);
    if (FAILED(hr)) return false;

    m_iconTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.8f), &m_blackBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_whiteBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.6f, 0.6f, 0.6f, 1), &m_grayBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.2f, 0.4f, 1), &m_themeBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.8f, 0.3f, 1.0f), &m_wifiBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.5f, 1.0f, 1.0f), &m_bluetoothBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.85f, 0.4f, 1.0f), &m_chargingBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.2f, 0.2f, 1.0f), &m_lowBatteryBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.6f, 1.0f, 1.0f), &m_fileBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.3f, 0.3f, 1.0f), &m_notificationBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f), &m_darkGrayBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.8f, 0.8f, 0.5f), &m_progressBgBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_progressFgBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_buttonHoverBrush);

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wicFactory));

    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    m_surface->BeginDraw(nullptr, __uuidof(IDXGISurface), reinterpret_cast<void**>(dxgiSurface.GetAddressOf()), &offset);

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> targetBitmap;
    m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bitmapProperties, &targetBitmap);
    m_d2dContext->SetTarget(targetBitmap.Get());
    m_surface->EndDraw();

    RegisterComponents();
    SetTheme(m_darkMode, m_primaryShellOpacity, m_secondaryShellOpacity);
    return true;
}

void RenderEngine::RegisterComponents() {
    m_sharedRes.d2dContext = m_d2dContext.Get();
    m_sharedRes.dwriteFactory = m_dwriteFactory.Get();
    m_sharedRes.d2dFactory = m_d2dFactory.Get();
    m_sharedRes.whiteBrush = m_whiteBrush.Get();
    m_sharedRes.grayBrush = m_grayBrush.Get();
    m_sharedRes.blackBrush = m_blackBrush.Get();
    m_sharedRes.themeBrush = m_themeBrush.Get();
    m_sharedRes.wifiBrush = m_wifiBrush.Get();
    m_sharedRes.bluetoothBrush = m_bluetoothBrush.Get();
    m_sharedRes.chargingBrush = m_chargingBrush.Get();
    m_sharedRes.lowBatteryBrush = m_lowBatteryBrush.Get();
    m_sharedRes.fileBrush = m_fileBrush.Get();
    m_sharedRes.notificationBrush = m_notificationBrush.Get();
    m_sharedRes.darkGrayBrush = m_darkGrayBrush.Get();
    m_sharedRes.progressBgBrush = m_progressBgBrush.Get();
    m_sharedRes.progressFgBrush = m_progressFgBrush.Get();
    m_sharedRes.buttonHoverBrush = m_buttonHoverBrush.Get();
    m_sharedRes.titleFormat = m_textFormatTitle.Get();
    m_sharedRes.subFormat = m_textFormatSub.Get();
    m_sharedRes.iconFormat = m_iconTextFormat.Get();
    m_sharedRes.wicFactory = m_wicFactory.Get();

    m_weatherComponent = std::make_unique<WeatherComponent>();
    m_weatherComponent->OnAttach(&m_sharedRes);

    m_lyricsComponent = std::make_unique<LyricsComponent>();
    m_lyricsComponent->OnAttach(&m_sharedRes);

    m_waveformComponent = std::make_unique<WaveformComponent>();
    m_waveformComponent->OnAttach(&m_sharedRes);

    m_alertComponent = std::make_unique<AlertComponent>();
    m_alertComponent->OnAttach(&m_sharedRes);

    m_volumeComponent = std::make_unique<VolumeComponent>();
    m_volumeComponent->OnAttach(&m_sharedRes);

    m_faceIdComponent = std::make_unique<FaceIdComponent>();
    m_faceIdComponent->OnAttach(&m_sharedRes);

    m_musicComponent = std::make_unique<MusicPlayerComponent>();
    m_musicComponent->OnAttach(&m_sharedRes);

    m_fileStorageComponent = std::make_unique<FilePanelComponent>();
    m_fileStorageComponent->OnAttach(&m_sharedRes);

    m_clockComponent = std::make_unique<ClockComponent>();
    m_clockComponent->OnAttach(&m_sharedRes);

    m_pomodoroComponent = std::make_unique<PomodoroComponent>();
    m_pomodoroComponent->OnAttach(&m_sharedRes);

    m_todoComponent = std::make_unique<TodoComponent>();
    m_todoComponent->OnAttach(&m_sharedRes);

    m_agentSessionsComponent = std::make_unique<AgentSessionsComponent>();
    m_agentSessionsComponent->OnAttach(&m_sharedRes);
}

void RenderEngine::SetDpi(float dpi) {
    m_dpi = dpi;
    if (m_d2dContext) {
        m_d2dContext->SetDpi(dpi, dpi);
    }
}

void RenderEngine::Resize(int width, int height) {
    if (!m_dcompDevice || !m_d2dContext) return;

    m_d2dContext->SetTarget(nullptr);
    m_surface.Reset();

    m_dcompDevice->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED, &m_surface);
    m_rootVisual->SetContent(m_surface.Get());
    m_dcompDevice->Commit();

    for (IIslandComponent* component : { static_cast<IIslandComponent*>(m_weatherComponent.get()), static_cast<IIslandComponent*>(m_lyricsComponent.get()),
        static_cast<IIslandComponent*>(m_waveformComponent.get()), static_cast<IIslandComponent*>(m_alertComponent.get()),
        static_cast<IIslandComponent*>(m_volumeComponent.get()), static_cast<IIslandComponent*>(m_musicComponent.get()),
        static_cast<IIslandComponent*>(m_faceIdComponent.get()), static_cast<IIslandComponent*>(m_fileStorageComponent.get()),
        static_cast<IIslandComponent*>(m_clockComponent.get()),
        static_cast<IIslandComponent*>(m_pomodoroComponent.get()), static_cast<IIslandComponent*>(m_todoComponent.get()),
        static_cast<IIslandComponent*>(m_agentSessionsComponent.get()) }) {
        if (component) {
            component->OnResize(m_dpi, width, height);
        }
    }
}

void RenderEngine::SetPlaybackState(bool hasSession, bool isPlaying, float progress,
    const std::wstring& title, const std::wstring& artist) {
    m_hasPlaybackSession = hasSession;
    m_isPlaybackActive = isPlaying;
    m_playbackTitle = title;
    m_playbackArtist = artist;
    if (m_musicComponent) {
        m_musicComponent->SetPlaybackState(hasSession, isPlaying, progress, title, artist);
    }
}

void RenderEngine::SetMusicArtworkStyles(MusicArtworkStyle compactStyle, MusicArtworkStyle expandedStyle) {
    if (m_musicComponent) {
        m_musicComponent->SetArtworkStyles(compactStyle, expandedStyle);
    }
}

void RenderEngine::SetMusicInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress) {
    if (m_musicComponent) {
        m_musicComponent->SetInteractionState(hoveredButton, pressedButton, hoveredProgress, pressedProgress);
    }
}

void RenderEngine::SetLyricData(const LyricData& lyric) {
    if (m_lyricsComponent) {
        m_lyricsComponent->SetLyric(lyric);
    }
}

void RenderEngine::SetWaveformState(const std::array<float, 3>& bandLevels, float islandHeight, WaveformDisplayStyle style) {
    if (m_waveformComponent) {
        m_waveformComponent->SetBandLevels(bandLevels);
        m_waveformComponent->SetIslandHeight(islandHeight);
        m_waveformComponent->SetDisplayStyle(style);
    }
}

void RenderEngine::SetTimeData(bool showTime, const std::wstring& timeText) {
    m_showTime = showTime;
    if (!timeText.empty()) {
        m_timeText = timeText;
    }
    if (m_clockComponent) {
        m_clockComponent->SetTimeData(showTime, timeText);
    }
}

void RenderEngine::SetVolumeState(bool active, float volumeLevel) {
    if (m_volumeComponent) {
        m_volumeComponent->SetActive(active);
        m_volumeComponent->SetVolumeLevel(volumeLevel);
    }
}

void RenderEngine::SetFaceUnlockState(FaceIdState state, const std::wstring& text) {
    if (m_faceIdComponent) {
        m_faceIdComponent->SetState(state, text);
    }
}

void RenderEngine::SetFileState(SecondaryContentKind mode, const std::vector<FileStashItem>& storedFiles, int selectedIndex, int hoveredIndex) {
    if (m_fileStorageComponent) {
        FilePanelComponent::ViewMode viewMode = FilePanelComponent::ViewMode::Hidden;
        switch (mode) {
        case SecondaryContentKind::FileCircle:
            viewMode = FilePanelComponent::ViewMode::Circle;
            break;
        case SecondaryContentKind::FileSwirlDrop:
            viewMode = FilePanelComponent::ViewMode::CircleDropTarget;
            break;
        case SecondaryContentKind::FileMini:
            viewMode = FilePanelComponent::ViewMode::Mini;
            break;
        case SecondaryContentKind::FileExpanded:
            viewMode = FilePanelComponent::ViewMode::Expanded;
            break;
        case SecondaryContentKind::FileDropTarget:
            viewMode = FilePanelComponent::ViewMode::DropTarget;
            break;
        default:
            viewMode = FilePanelComponent::ViewMode::Hidden;
            break;
        }
        m_fileStorageComponent->SetViewMode(viewMode);
        m_fileStorageComponent->SetStoredFiles(storedFiles);
        m_fileStorageComponent->SetInteractionState(selectedIndex, hoveredIndex);
    }
}

void RenderEngine::SetWeatherState(const std::wstring& locationText, const std::wstring& desc, float temp, const std::wstring& iconId,
    const std::vector<HourlyForecast>& hourly, const std::vector<DailyForecast>& daily,
    bool expanded, WeatherViewMode viewMode, bool available) {
    m_weatherLocationText = locationText;
    m_weatherDesc = desc;
    m_weatherTemp = temp;
    m_weatherIconId = iconId;
    m_weatherAvailable = available;
    if (m_weatherComponent) {
        m_weatherComponent->SetWeatherData(locationText, desc, temp, iconId, hourly, daily, available);
        m_weatherComponent->SetExpanded(expanded);
        m_weatherComponent->SetViewMode(viewMode);
    }
}

void RenderEngine::SetAlertState(bool active, const AlertInfo& info) {
    if (m_alertComponent) {
        m_alertComponent->SetAlertState(active, info);
        if (!active) {
            m_alertComponent->ClearAlertBitmap();
        }
    }
}

void RenderEngine::SetTodoStore(TodoStore* store) {
    m_todoStore = store;
    if (m_todoComponent) {
        m_todoComponent->SetStore(store);
    }
}

void RenderEngine::SetAgentSessionState(const std::vector<AgentSessionSummary>& summaries,
    AgentSessionFilter filter,
    AgentKind selectedKind,
    const std::wstring& selectedSessionId,
    const std::vector<AgentHistoryEntry>& selectedHistory,
    AgentKind compactProvider,
    bool chooserOpen) {
    m_agentSummaries = summaries;
    m_compactAgentProvider = compactProvider;
    if (!m_agentSessionsComponent) {
        return;
    }

    m_agentSessionsComponent->SetSessions(summaries);
    m_agentSessionsComponent->SetFilter(filter);
    m_agentSessionsComponent->SetSelectedSession(selectedKind, selectedSessionId, selectedHistory);
    m_agentSessionsComponent->SetCompactState(compactProvider, chooserOpen);
}

void RenderEngine::SetTheme(bool darkMode, float primaryOpacity, float secondaryOpacity) {
    m_darkMode = darkMode;
    m_primaryShellOpacity = (std::max)(0.3f, (std::min)(1.0f, primaryOpacity));
    m_secondaryShellOpacity = (std::max)(0.3f, (std::min)(1.0f, secondaryOpacity));

    if (!m_d2dContext) {
        return;
    }

    const D2D1_COLOR_F shellColor = darkMode
        ? D2D1::ColorF(0.08f, 0.08f, 0.10f, 1.0f)
        : D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f);
    const D2D1_COLOR_F textColor = darkMode
        ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
        : D2D1::ColorF(0.10f, 0.10f, 0.12f, 1.0f);
    const D2D1_COLOR_F secondaryText = darkMode
        ? D2D1::ColorF(0.65f, 0.65f, 0.68f, 1.0f)
        : D2D1::ColorF(0.36f, 0.36f, 0.40f, 1.0f);

    if (m_blackBrush) m_blackBrush->SetColor(shellColor);
    if (m_whiteBrush) m_whiteBrush->SetColor(textColor);
    if (m_grayBrush) m_grayBrush->SetColor(secondaryText);
    if (m_darkGrayBrush) m_darkGrayBrush->SetColor(darkMode ? D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f) : D2D1::ColorF(0.78f, 0.78f, 0.82f, 1.0f));
    if (m_progressBgBrush) m_progressBgBrush->SetColor(darkMode ? D2D1::ColorF(0.8f, 0.8f, 0.8f, 0.5f) : D2D1::ColorF(0.18f, 0.18f, 0.22f, 0.24f));
    if (m_progressFgBrush) m_progressFgBrush->SetColor(textColor);
    if (m_buttonHoverBrush) m_buttonHoverBrush->SetColor(darkMode ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.12f, 0.12f, 0.16f, 1.0f));
    if (m_todoComponent) m_todoComponent->SetDarkMode(darkMode);
    if (m_agentSessionsComponent) m_agentSessionsComponent->SetDarkMode(darkMode);
}

void RenderEngine::SetClockClickCallback(std::function<void()> callback) {
    if (m_clockComponent) {
        m_clockComponent->SetOnClick(std::move(callback));
    }
}

bool RenderEngine::LoadAlbumArt(const std::wstring& file) {
    return m_musicComponent ? m_musicComponent->LoadAlbumArt(file) : false;
}

bool RenderEngine::LoadAlbumArtFromMemory(const std::vector<uint8_t>& data) {
    return m_musicComponent ? m_musicComponent->LoadAlbumArtFromMemory(data) : false;
}

bool RenderEngine::LoadAlertIcon(const std::wstring& file) {
    return m_alertComponent ? m_alertComponent->LoadAlertIcon(file) : false;
}

bool RenderEngine::LoadAlertIconFromMemory(const std::vector<uint8_t>& data) {
    return m_alertComponent ? m_alertComponent->LoadAlertIconFromMemory(data) : false;
}

void RenderEngine::UpdateComponents(float deltaTime, const RenderContext& ctx) {
    if (m_musicComponent) {
        m_musicComponent->SetCompactMode(ctx.mode == IslandDisplayMode::MusicCompact);
        m_musicComponent->Update(deltaTime);
    }
    if (m_lyricsComponent) m_lyricsComponent->Update(deltaTime);
    if (m_waveformComponent) m_waveformComponent->Update(deltaTime);
    if (m_weatherComponent) m_weatherComponent->Update(deltaTime);
    if (m_alertComponent) m_alertComponent->Update(deltaTime);
    if (m_volumeComponent) m_volumeComponent->Update(deltaTime);
    if (m_faceIdComponent) m_faceIdComponent->Update(deltaTime);
    if (m_fileStorageComponent) m_fileStorageComponent->Update(deltaTime);
    if (m_clockComponent) m_clockComponent->Update(deltaTime);
    if (m_pomodoroComponent) m_pomodoroComponent->Update(deltaTime);
    if (m_todoComponent) {
        m_todoComponent->SetDisplayMode(ctx.mode);
        m_todoComponent->Update(deltaTime);
    }
    if (m_agentSessionsComponent) {
        m_agentSessionsComponent->SetDisplayMode(ctx.mode);
        m_agentSessionsComponent->Update(deltaTime);
    }
}

IIslandComponent* RenderEngine::ResolvePrimaryComponent(IslandDisplayMode mode) {
    switch (mode) {
    case IslandDisplayMode::Shrunk:
        return nullptr;
    case IslandDisplayMode::Alert:
        return m_alertComponent.get();
    case IslandDisplayMode::FaceUnlockFeedback:
        return m_faceIdComponent.get();
    case IslandDisplayMode::TodoInputCompact:
    case IslandDisplayMode::TodoListCompact:
    case IslandDisplayMode::TodoExpanded:
        return m_todoComponent.get();
    case IslandDisplayMode::AgentCompact:
    case IslandDisplayMode::AgentExpanded:
        return m_agentSessionsComponent.get();
    case IslandDisplayMode::PomodoroExpanded:
    case IslandDisplayMode::PomodoroCompact:
        return m_pomodoroComponent.get();
    case IslandDisplayMode::WeatherExpanded:
        return m_weatherComponent.get();
    case IslandDisplayMode::FileDrop:
        return m_fileStorageComponent.get();
    case IslandDisplayMode::MusicExpanded:
    case IslandDisplayMode::MusicCompact:
        return m_musicComponent.get();
    case IslandDisplayMode::Volume:
        return m_volumeComponent.get();
    case IslandDisplayMode::Idle:
    default:
        return m_clockComponent.get();
    }
}

IIslandComponent* RenderEngine::ResolveSecondaryComponent(SecondaryContentKind kind) {
    switch (kind) {
    case SecondaryContentKind::Volume:
        return m_volumeComponent.get();
    case SecondaryContentKind::FileCircle:
    case SecondaryContentKind::FileSwirlDrop:
    case SecondaryContentKind::FileMini:
    case SecondaryContentKind::FileExpanded:
    case SecondaryContentKind::FileDropTarget:
        return m_fileStorageComponent.get();
    case SecondaryContentKind::None:
    default:
        return nullptr;
    }
}

void RenderEngine::DrawPrimaryContent(const D2D1_RECT_F& contentRect, const RenderContext& ctx) {
    m_activePrimaryComponent = ResolvePrimaryComponent(ctx.mode);
    D2D1_RECT_F primaryRect = contentRect;
    const bool showWorkingEdgeBadge =
        m_agentSessionsComponent &&
        m_agentSessionsComponent->ShouldShowWorkingEdgeBadge(ctx.mode, ctx.islandHeight);
    if (showWorkingEdgeBadge) {
        const D2D1_RECT_F badgeRect = D2D1::RectF(
            contentRect.left + 2.0f,
            contentRect.top + 3.0f,
            contentRect.left + Constants::Size::AGENT_EDGE_BADGE_WIDTH - 2.0f,
            contentRect.bottom - 3.0f);
        m_agentSessionsComponent->DrawWorkingEdgeBadge(badgeRect, ctx.contentAlpha);
        primaryRect.left += Constants::Size::AGENT_EDGE_BADGE_WIDTH;
    }

    switch (ctx.mode) {
    case IslandDisplayMode::Shrunk:
        break;
    case IslandDisplayMode::Alert:
    case IslandDisplayMode::FaceUnlockFeedback:
    case IslandDisplayMode::PomodoroExpanded:
    case IslandDisplayMode::WeatherExpanded:
    case IslandDisplayMode::Volume:
        if (m_activePrimaryComponent) {
            m_activePrimaryComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::FileDrop:
        if (m_fileStorageComponent) {
            m_fileStorageComponent->SetViewMode(FilePanelComponent::ViewMode::SwirlDrop);
            m_fileStorageComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::PomodoroCompact:
        if (m_activePrimaryComponent) {
            m_activePrimaryComponent->Draw(primaryRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::TodoInputCompact:
    case IslandDisplayMode::TodoListCompact:
    case IslandDisplayMode::TodoExpanded:
    case IslandDisplayMode::AgentCompact:
    case IslandDisplayMode::AgentExpanded:
        if (m_activePrimaryComponent) {
            m_activePrimaryComponent->Draw(primaryRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::MusicCompact:
        if (m_musicComponent) m_musicComponent->Draw(primaryRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_lyricsComponent) {
            D2D1_RECT_F lyricRect = BuildMusicLyricRect(primaryRect, ctx.islandHeight);
            m_lyricsComponent->DrawCompact(lyricRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        if (m_waveformComponent) m_waveformComponent->Draw(primaryRect, ctx.contentAlpha, ctx.currentTimeMs);
        break;
    case IslandDisplayMode::MusicExpanded:
        if (m_waveformComponent) m_waveformComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_musicComponent) m_musicComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_lyricsComponent) {
            D2D1_RECT_F lyricRect = BuildMusicLyricRect(contentRect, ctx.islandHeight);
            m_lyricsComponent->DrawCompact(lyricRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::Idle:
    default:
        if (ctx.islandHeight < 35.0f) {
            if (m_todoComponent) {
                m_todoComponent->DrawIdleBadge(D2D1::RectF(0, 0, 0, 0), 0.0f, ctx.currentTimeMs);
            }
            break;
        }
        const D2D1_RECT_F todoBadgeRect = D2D1::RectF(primaryRect.left + 8.0f, primaryRect.top + 6.0f, primaryRect.left + 36.0f, primaryRect.bottom - 6.0f);
        const bool todoInputPresentation =
            m_todoComponent && m_todoComponent->GetDisplayMode() == IslandDisplayMode::TodoInputCompact;
        if (todoInputPresentation) {
            m_activePrimaryComponent = m_todoComponent.get();
            m_todoComponent->Draw(primaryRect, ctx.contentAlpha, ctx.currentTimeMs);
            break;
        }
        if (m_todoComponent) {
            if (!m_todoComponent->IsLaunchAnimating()) {
                m_todoComponent->DrawIdleBadge(todoBadgeRect, ctx.contentAlpha, ctx.currentTimeMs);
            }
        }
        if (m_clockComponent) {
            const float weatherReserve = 54.0f;
            const D2D1_RECT_F clockRect = D2D1::RectF(primaryRect.left + 42.0f, primaryRect.top, primaryRect.right - weatherReserve, primaryRect.bottom);
            m_clockComponent->Draw(clockRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        if (m_weatherComponent) {
            float iconSize = (primaryRect.bottom - primaryRect.top) * 0.4f;
            float iconX = primaryRect.right - iconSize - 15.0f;
            float iconY = primaryRect.top + ((primaryRect.bottom - primaryRect.top) - iconSize) / 2.0f;
            m_weatherComponent->DrawCompact(iconX, iconY, iconSize, ctx.contentAlpha, ctx.currentTimeMs);
        }
        if (m_todoComponent && m_todoComponent->IsLaunchAnimating()) {
            m_todoComponent->DrawIdleLaunchOverlay(primaryRect, todoBadgeRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    }
}

void RenderEngine::DrawSecondaryIsland(const RenderContext& ctx, float top, float bottom) {
    if (ctx.secondaryHeight <= 0.1f || ctx.secondaryAlpha <= 0.01f || ctx.secondaryContent == SecondaryContentKind::None) {
        m_activeSecondaryComponent = nullptr;
        m_lastSecondaryRect = D2D1::RectF(0, 0, 0, 0);
        return;
    }

    float secWidth = Constants::Size::SECONDARY_WIDTH;
    switch (ctx.secondaryContent) {
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
    float secLeft = (ctx.canvasWidth - secWidth) / 2.0f;
    float secTop = bottom + 12.0f;
    if (ctx.secondaryContent == SecondaryContentKind::FileCircle ||
        ctx.secondaryContent == SecondaryContentKind::FileSwirlDrop) {
        const float islandLeft = (ctx.canvasWidth - ctx.islandWidth) / 2.0f;
        secLeft = islandLeft + ctx.islandWidth + Constants::Size::FILE_CIRCLE_GAP;
        secTop = top + (ctx.islandHeight - ctx.secondaryHeight) * 0.5f;
        if (secTop < top + 4.0f) {
            secTop = top + 4.0f;
        }
    }
    float secRight = secLeft + secWidth;
    float secBottom = secTop + ctx.secondaryHeight;
    float secRadius = (ctx.secondaryHeight < 60.0f) ? (ctx.secondaryHeight / 2.0f) : 20.0f;

    D2D1_ROUNDED_RECT secRect = D2D1::RoundedRect(D2D1::RectF(secLeft, secTop, secRight, secBottom), secRadius, secRadius);
    float shellAlpha = (ctx.secondaryContent == SecondaryContentKind::Volume) ? ctx.secondaryAlpha : (0.35f + 0.65f * ctx.secondaryAlpha);
    shellAlpha *= m_secondaryShellOpacity;
    m_blackBrush->SetOpacity(shellAlpha);
    m_d2dContext->FillRoundedRectangle(&secRect, m_blackBrush.Get());
    m_activeSecondaryComponent = ResolveSecondaryComponent(ctx.secondaryContent);
    m_lastSecondaryRect = secRect.rect;
    if (ctx.secondaryContent == SecondaryContentKind::Volume && m_volumeComponent) {
        m_volumeComponent->DrawSecondary(secLeft, secTop, secWidth, ctx.secondaryHeight, ctx.secondaryAlpha);
    } else if (m_fileStorageComponent) {
        if (ctx.secondaryContent == SecondaryContentKind::FileCircle) {
            m_fileStorageComponent->SetViewMode(FilePanelComponent::ViewMode::Circle);
        } else if (ctx.secondaryContent == SecondaryContentKind::FileSwirlDrop) {
            m_fileStorageComponent->SetViewMode(FilePanelComponent::ViewMode::CircleDropTarget);
        }
        m_fileStorageComponent->Draw(secRect.rect, ctx.secondaryAlpha, ctx.currentTimeMs);
    }
}

D2D1_RECT_F RenderEngine::BuildShrinkInterpolatedRect(const RenderContext& ctx, float top) const {
    float sourceWidth = Constants::Size::COMPACT_WIDTH;
    float sourceHeight = Constants::Size::COMPACT_HEIGHT;
    switch (ctx.shrinkSourceMode) {
    case IslandDisplayMode::MusicCompact:
        sourceWidth = Constants::Size::MUSIC_COMPACT_WIDTH;
        sourceHeight = Constants::Size::MUSIC_COMPACT_HEIGHT;
        break;
    case IslandDisplayMode::PomodoroCompact:
        sourceWidth = Constants::Size::POMODORO_COMPACT_WIDTH;
        sourceHeight = Constants::Size::POMODORO_COMPACT_HEIGHT;
        break;
    case IslandDisplayMode::TodoListCompact:
    case IslandDisplayMode::AgentCompact:
    case IslandDisplayMode::Idle:
    default:
        break;
    }

    const D2D1_RECT_F sourceRect = D2D1::RectF(
        (ctx.canvasWidth - sourceWidth) * 0.5f,
        top,
        (ctx.canvasWidth + sourceWidth) * 0.5f,
        top + sourceHeight);
    const float shrunkHeight = (std::max)(Constants::Size::SHRUNK_HEIGHT, 6.0f);
    const D2D1_RECT_F shrunkRect = D2D1::RectF(
        (ctx.canvasWidth - Constants::Size::SHRUNK_WIDTH) * 0.5f,
        top,
        (ctx.canvasWidth + Constants::Size::SHRUNK_WIDTH) * 0.5f,
        top + shrunkHeight);
    return LerpRect(sourceRect, shrunkRect, Clamp01(ctx.shrinkProgress));
}

void RenderEngine::DrawShrinkHandle(const D2D1_RECT_F& rect, float alpha) {
    const float width = (std::min)(28.0f, (std::max)(12.0f, rect.right - rect.left - 10.0f));
    const float height = 2.0f;
    const float left = (rect.left + rect.right - width) * 0.5f;
    const D2D1_RECT_F handleRect = D2D1::RectF(left, rect.top + 1.5f, left + width, rect.top + 1.5f + height);
    const D2D1_ROUNDED_RECT handle = D2D1::RoundedRect(handleRect, height * 0.5f, height * 0.5f);
    m_whiteBrush->SetOpacity(alpha);
    m_d2dContext->FillRoundedRectangle(&handle, m_whiteBrush.Get());
}

void RenderEngine::DrawCompactShrinkHandle(const D2D1_RECT_F& rect, float alpha) {
    const float handleWidth = 32.0f;
    const float handleHeight = 2.5f;
    const float handleLeft = (rect.left + rect.right - handleWidth) * 0.5f;
    const D2D1_RECT_F handleRect = D2D1::RectF(
        handleLeft,
        rect.top + 4.0f,
        handleLeft + handleWidth,
        rect.top + 4.0f + handleHeight);
    D2D1_ROUNDED_RECT handle = D2D1::RoundedRect(handleRect, handleHeight * 0.5f, handleHeight * 0.5f);
    m_whiteBrush->SetOpacity(alpha);
    m_d2dContext->FillRoundedRectangle(&handle, m_whiteBrush.Get());
}

bool RenderEngine::TryGetGhostHudText(IslandDisplayMode sourceMode, std::wstring& primary, std::wstring& secondary, bool& musicActive) const {
    musicActive = false;
    auto fillMusic = [&]() {
        primary = m_playbackTitle.empty() ? L"Music" : m_playbackTitle;
        secondary = m_playbackArtist.empty() ? (m_isPlaybackActive ? L"Playing" : L"Paused") : m_playbackArtist;
        musicActive = true;
        return true;
    };
    auto fillAgent = [&]() {
        auto preferred = std::find_if(m_agentSummaries.begin(), m_agentSummaries.end(),
        [this](const AgentSessionSummary& summary) {
            return summary.kind == m_compactAgentProvider && (summary.isLive || !summary.statusText.empty());
        });
        if (preferred == m_agentSummaries.end()) {
            preferred = std::find_if(m_agentSummaries.begin(), m_agentSummaries.end(),
                [](const AgentSessionSummary& summary) {
                    return summary.isLive || !summary.statusText.empty() || !summary.title.empty();
                });
        }
        if (preferred != m_agentSummaries.end()) {
            primary = AgentKindLabel(preferred->kind);
            const std::wstring phase = AgentPhaseLabel(preferred->phase);
            if (!preferred->statusText.empty()) {
                secondary = preferred->statusText;
            } else if (!preferred->recentActivityText.empty()) {
                secondary = preferred->recentActivityText;
            } else if (!phase.empty()) {
                secondary = phase;
            } else {
                secondary = preferred->title;
            }
        } else {
            primary = AgentKindLabel(m_compactAgentProvider);
            secondary = L"No active session";
        }
        return true;
    };
    auto fillTodo = [&]() {
        primary = L"Todo";
        if (m_todoStore) {
            const size_t incomplete = m_todoStore->CountIncomplete();
            const TodoItem* item = m_todoStore->GetTopIncomplete();
            if (item && !item->title.empty()) {
                secondary = item->title;
            } else {
                secondary = std::to_wstring(incomplete) + L" pending";
            }
        } else {
            secondary = L"No todo data";
        }
        return true;
    };
    auto fillPomodoro = [&]() {
        primary = L"Pomodoro";
        if (m_pomodoroComponent && m_pomodoroComponent->HasActiveSession()) {
            const int remaining = (std::max)(0, m_pomodoroComponent->GetRemainingSeconds());
            wchar_t timeText[32] = {};
            swprintf_s(timeText, L"%02d:%02d", remaining / 60, remaining % 60);
            if (m_pomodoroComponent->IsRunning()) {
                secondary = std::wstring(timeText) + L" running";
            } else if (m_pomodoroComponent->IsPaused()) {
                secondary = std::wstring(timeText) + L" paused";
            } else {
                secondary = timeText;
            }
        } else {
            secondary = L"No active timer";
        }
        return true;
    };
    auto fillIdle = [&]() {
        primary = m_timeText.empty() ? L"--:--" : m_timeText;
        if (m_weatherAvailable) {
            wchar_t tempText[32] = {};
            swprintf_s(tempText, L"%.0f C", m_weatherTemp);
            secondary = m_weatherDesc.empty() ? tempText : (m_weatherDesc + L"  " + tempText);
        } else {
            secondary = L"Weather unavailable";
        }
        return true;
    };

    switch (sourceMode) {
    case IslandDisplayMode::MusicCompact:
        return fillMusic();
    case IslandDisplayMode::AgentCompact:
        return false;
    case IslandDisplayMode::TodoListCompact:
        return fillTodo();
    case IslandDisplayMode::PomodoroCompact:
        return fillPomodoro();
    case IslandDisplayMode::Idle:
    default:
        return fillIdle();
    }
}

void RenderEngine::DrawShrunkGhostHud(const RenderContext& ctx, float alpha) {
    if (!m_d2dContext || alpha <= 0.01f) {
        return;
    }

    const float width = Constants::Size::COMPACT_WIDTH;
    const float height = Constants::Size::COMPACT_HEIGHT;
    const float left = (ctx.canvasWidth - width) * 0.5f;
    const float top = Constants::UI::TOP_MARGIN;
    const D2D1_RECT_F hudRect = D2D1::RectF(left, top, left + width, top + height);

    if (ctx.shrinkSourceMode == IslandDisplayMode::Idle) {
        const float hudAlpha = (std::min)(0.56f, alpha * 1.20f);
        if (m_clockComponent) {
            const float weatherReserve = 54.0f;
            const D2D1_RECT_F clockRect = D2D1::RectF(hudRect.left + 42.0f, hudRect.top, hudRect.right - weatherReserve, hudRect.bottom);
            m_clockComponent->Draw(clockRect, hudAlpha, ctx.currentTimeMs);
        }
        if (m_weatherComponent) {
            const float iconSize = (hudRect.bottom - hudRect.top) * 0.4f;
            const float iconX = hudRect.right - iconSize - 15.0f;
            const float iconY = hudRect.top + ((hudRect.bottom - hudRect.top) - iconSize) * 0.5f;
            m_weatherComponent->DrawCompact(iconX, iconY, iconSize, hudAlpha, ctx.currentTimeMs);
        }
        return;
    }

    std::wstring primary;
    std::wstring secondary;
    bool musicActive = false;
    if (!TryGetGhostHudText(ctx.shrinkSourceMode, primary, secondary, musicActive)) {
        return;
    }

    if (musicActive && m_waveformComponent) {
        D2D1_RECT_F bars = D2D1::RectF(hudRect.right - 46.0f, hudRect.top + 10.0f, hudRect.right - 12.0f, hudRect.bottom - 10.0f);
        m_waveformComponent->Draw(bars, alpha * 0.65f, ctx.currentTimeMs);
    }

    const float textRight = musicActive ? hudRect.right - 54.0f : hudRect.right - 12.0f;
    const D2D1_RECT_F primaryRect = D2D1::RectF(hudRect.left + 14.0f, hudRect.top + 6.0f, textRight, hudRect.top + 23.0f);
    const D2D1_RECT_F secondaryRect = D2D1::RectF(hudRect.left + 14.0f, hudRect.top + 23.0f, textRight, hudRect.bottom - 5.0f);

    m_whiteBrush->SetOpacity(alpha);
    m_grayBrush->SetOpacity(alpha * 0.90f);
    m_d2dContext->DrawTextW(primary.c_str(), static_cast<UINT32>(primary.size()), m_textFormatTitle.Get(), primaryRect, m_whiteBrush.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    m_d2dContext->DrawTextW(secondary.c_str(), static_cast<UINT32>(secondary.size()), m_textFormatSub.Get(), secondaryRect, m_grayBrush.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void RenderEngine::DrawCapsule(const RenderContext& ctx) {
    if (!m_surface) return;

    if (m_lastFrameTime == 0) m_lastFrameTime = ctx.currentTimeMs;
    float deltaTime = static_cast<float>(ctx.currentTimeMs - m_lastFrameTime) / 1000.0f;
    if (deltaTime < 0.0f || deltaTime > 0.5f) deltaTime = 0.016f;
    m_lastFrameTime = ctx.currentTimeMs;

    UpdateComponents(deltaTime, ctx);
    m_lastMode = ctx.mode;

    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    m_surface->BeginDraw(nullptr, __uuidof(IDXGISurface), reinterpret_cast<void**>(dxgiSurface.GetAddressOf()), &offset);

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        m_dpi, m_dpi);

    ComPtr<ID2D1Bitmap1> d2dTargetBitmap;
    m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bitmapProperties, &d2dTargetBitmap);
    m_d2dContext->SetTarget(d2dTargetBitmap.Get());
    m_d2dContext->BeginDraw();
    m_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    float dpiX = 96.0f;
    float dpiY = 96.0f;
    m_d2dContext->GetDpi(&dpiX, &dpiY);
    float dpiScale = dpiX / 96.0f;
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x) / dpiScale, static_cast<float>(offset.y) / dpiScale));

    const float top = Constants::UI::TOP_MARGIN;
    float left = (ctx.canvasWidth - ctx.islandWidth) / 2.0f;
    float right = left + ctx.islandWidth;
    float bottom = top + ctx.islandHeight;
    float radius = (std::min)(Constants::UI::NOTCH_BOTTOM_RADIUS, ctx.islandHeight / 2.0f);
    D2D1_RECT_F notchRect = D2D1::RectF(left, top, right, bottom);
    const bool shrinkVisual = ctx.mode == IslandDisplayMode::Shrunk || ctx.shrinkAnimating;
    D2D1_RECT_F shellRect = shrinkVisual ? BuildShrinkInterpolatedRect(ctx, top) : notchRect;
    if (shrinkVisual) {
        left = shellRect.left;
        right = shellRect.right;
        bottom = shellRect.bottom;
        radius = (shellRect.bottom - shellRect.top) * 0.5f;
    }
    D2D1_ROUNDED_RECT fallbackRect = D2D1::RoundedRect(shellRect, radius, radius);
    ComPtr<ID2D1PathGeometry> notchGeometry = shrinkVisual ? nullptr : CreateNotchGeometry(m_d2dFactory.Get(), shellRect, radius);

    m_blackBrush->SetOpacity(m_primaryShellOpacity);
    if (notchGeometry) {
        m_d2dContext->FillGeometry(notchGeometry.Get(), m_blackBrush.Get());
    } else {
        m_d2dContext->FillRoundedRectangle(&fallbackRect, m_blackBrush.Get());
    }

    const float contentFade = shrinkVisual ? (1.0f - Clamp01(ctx.shrinkProgress)) : 1.0f;
    const float contentAlpha = ctx.contentAlpha * contentFade;
    IslandDisplayMode contentMode = ctx.mode;
    if (ctx.mode == IslandDisplayMode::Shrunk && ctx.shrinkAnimating && ctx.shrinkProgress < 0.98f) {
        contentMode = ctx.shrinkSourceMode;
    }

    if (contentMode != IslandDisplayMode::Shrunk && contentAlpha > 0.01f) {
        ComPtr<ID2D1Geometry> clipGeometry;
        if (notchGeometry) {
            clipGeometry = notchGeometry;
        } else {
            ComPtr<ID2D1RoundedRectangleGeometry> fallbackGeometry;
            if (SUCCEEDED(m_d2dFactory->CreateRoundedRectangleGeometry(&fallbackRect, &fallbackGeometry))) {
                clipGeometry = fallbackGeometry;
            }
        }

        ComPtr<ID2D1Layer> layer;
        if (clipGeometry && SUCCEEDED(m_d2dContext->CreateLayer(&layer))) {
            m_d2dContext->PushLayer(D2D1::LayerParameters(
                D2D1::InfiniteRect(), clipGeometry.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE), layer.Get());
        }

        m_whiteBrush->SetOpacity(contentAlpha);
        m_grayBrush->SetOpacity(contentAlpha);
        m_themeBrush->SetOpacity(contentAlpha);

        RenderContext contentCtx = ctx;
        contentCtx.mode = contentMode;
        contentCtx.contentAlpha = contentAlpha;
        DrawPrimaryContent(notchRect, contentCtx);

        if (layer) {
            m_d2dContext->PopLayer();
        }
    }

    const bool compactHandleVisible =
        ctx.mode == IslandDisplayMode::Idle ||
        ctx.mode == IslandDisplayMode::MusicCompact ||
        ctx.mode == IslandDisplayMode::PomodoroCompact ||
        ctx.mode == IslandDisplayMode::TodoListCompact ||
        ctx.mode == IslandDisplayMode::AgentCompact;
    if (compactHandleVisible && ctx.islandHeight >= Constants::Size::COMPACT_MIN_HEIGHT) {
        DrawCompactShrinkHandle(notchRect, 0.36f * contentAlpha);
    }

    if (shrinkVisual) {
        const float ghostAlpha = Clamp01((ctx.shrinkProgress - 0.45f) / 0.55f) * 0.46f;
        DrawShrunkGhostHud(ctx, ghostAlpha);
        DrawShrinkHandle(shellRect, 0.56f * ctx.contentAlpha);
    }

    DrawSecondaryIsland(ctx, top, bottom);

    m_d2dContext->EndDraw();
    m_surface->EndDraw();
    m_dcompDevice->Commit();
}

bool RenderEngine::OnMouseWheel(float x, float y, int delta) {
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnMouseWheel(x, y, delta) : false;
}

bool RenderEngine::OnMouseMove(float x, float y) {
    if (m_activeSecondaryComponent &&
        x >= m_lastSecondaryRect.left && x <= m_lastSecondaryRect.right &&
        y >= m_lastSecondaryRect.top && y <= m_lastSecondaryRect.bottom) {
        return m_activeSecondaryComponent->OnMouseMove(x, y);
    }
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnMouseMove(x, y) : false;
}

bool RenderEngine::OnMouseClick(float x, float y) {
    if (m_activeSecondaryComponent &&
        x >= m_lastSecondaryRect.left && x <= m_lastSecondaryRect.right &&
        y >= m_lastSecondaryRect.top && y <= m_lastSecondaryRect.bottom) {
        return m_activeSecondaryComponent->OnMouseClick(x, y);
    }
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnMouseClick(x, y) : false;
}

bool RenderEngine::OnChar(wchar_t ch) {
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnChar(ch) : false;
}

bool RenderEngine::OnKeyDown(WPARAM key) {
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnKeyDown(key) : false;
}

bool RenderEngine::OnImeComposition(HWND hwnd, LPARAM lParam) {
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnImeComposition(hwnd, lParam) : false;
}

bool RenderEngine::OnImeSetContext(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    return m_activePrimaryComponent ? m_activePrimaryComponent->OnImeSetContext(hwnd, wParam, lParam, result) : false;
}

D2D1_RECT_F RenderEngine::GetImeAnchorRect() const {
    return m_activePrimaryComponent ? m_activePrimaryComponent->GetImeAnchorRect() : D2D1::RectF(0, 0, 0, 0);
}

bool RenderEngine::HandleIdleTodoMouseMove(float x, float y) {
    return (m_lastMode == IslandDisplayMode::Idle && m_todoComponent)
        ? m_todoComponent->OnIdleBadgeMouseMove(x, y)
        : false;
}

bool RenderEngine::IdleTodoBadgeContains(float x, float y) const {
    return (m_lastMode == IslandDisplayMode::Idle && m_todoComponent)
        ? m_todoComponent->IdleBadgeContains(x, y)
        : false;
}

bool RenderEngine::SecondaryContainsPoint(float x, float y) const {
    return x >= m_lastSecondaryRect.left && x <= m_lastSecondaryRect.right &&
        y >= m_lastSecondaryRect.top && y <= m_lastSecondaryRect.bottom;
}

FilePanelComponent::HitResult RenderEngine::HitTestFileSecondary(float x, float y) const {
    if (!m_fileStorageComponent) return {};
    return m_fileStorageComponent->HitTest(x, y);
}

bool RenderEngine::HasActiveAnimations() const {
    return (m_musicComponent && m_musicComponent->NeedsRender()) ||
        (m_lyricsComponent && m_lyricsComponent->NeedsRender()) ||
        (m_waveformComponent && m_waveformComponent->NeedsRender()) ||
        (m_weatherComponent && m_weatherComponent->NeedsRender()) ||
        (m_faceIdComponent && m_faceIdComponent->NeedsRender()) ||
        (m_pomodoroComponent && m_pomodoroComponent->NeedsRender()) ||
        (m_todoComponent && m_todoComponent->NeedsRender()) ||
        (m_agentSessionsComponent && m_agentSessionsComponent->NeedsRender());
}

