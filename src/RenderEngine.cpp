#include "RenderEngine.h"
#include "Constants.h"
#include <cmath>

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

    m_musicComponent = std::make_unique<MusicPlayerComponent>();
    m_musicComponent->OnAttach(&m_sharedRes);

    m_fileStorageComponent = std::make_unique<FilePanelComponent>();
    m_fileStorageComponent->OnAttach(&m_sharedRes);

    m_clockComponent = std::make_unique<ClockComponent>();
    m_clockComponent->OnAttach(&m_sharedRes);
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
        static_cast<IIslandComponent*>(m_fileStorageComponent.get()), static_cast<IIslandComponent*>(m_clockComponent.get()) }) {
        if (component) {
            component->OnResize(m_dpi, width, height);
        }
    }
}

void RenderEngine::SetPlaybackState(bool hasSession, bool isPlaying, float progress,
    const std::wstring& title, const std::wstring& artist) {
    if (m_musicComponent) {
        m_musicComponent->SetPlaybackState(hasSession, isPlaying, progress, title, artist);
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

void RenderEngine::SetWaveformState(float audioLevel, float islandHeight) {
    if (m_waveformComponent) {
        m_waveformComponent->SetAudioLevel(audioLevel);
        m_waveformComponent->SetIslandHeight(islandHeight);
    }
}

void RenderEngine::SetTimeData(bool showTime, const std::wstring& timeText) {
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

void RenderEngine::SetFileState(SecondaryContentKind mode, const std::vector<FileStashItem>& storedFiles, int selectedIndex, int hoveredIndex) {
    if (m_fileStorageComponent) {
        FilePanelComponent::ViewMode viewMode = FilePanelComponent::ViewMode::Hidden;
        switch (mode) {
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

void RenderEngine::SetWeatherState(const std::wstring& desc, float temp, const std::wstring& iconId,
    const std::vector<HourlyForecast>& hourly, const std::vector<DailyForecast>& daily,
    bool expanded, WeatherViewMode viewMode) {
    if (m_weatherComponent) {
        m_weatherComponent->SetWeatherData(desc, temp, iconId, hourly, daily);
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
    if (m_fileStorageComponent) m_fileStorageComponent->Update(deltaTime);
    if (m_clockComponent) m_clockComponent->Update(deltaTime);
}

IIslandComponent* RenderEngine::ResolvePrimaryComponent(IslandDisplayMode mode) {
    switch (mode) {
    case IslandDisplayMode::Alert:
        return m_alertComponent.get();
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

    switch (ctx.mode) {
    case IslandDisplayMode::Alert:
    case IslandDisplayMode::WeatherExpanded:
    case IslandDisplayMode::FileDrop:
    case IslandDisplayMode::Volume:
        if (m_activePrimaryComponent) {
            m_activePrimaryComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::MusicCompact:
        if (m_waveformComponent) m_waveformComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_musicComponent) m_musicComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        break;
    case IslandDisplayMode::MusicExpanded:
        if (m_waveformComponent) m_waveformComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_musicComponent) m_musicComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_lyricsComponent) {
            float lyricLeft = contentRect.left + 95.0f;
            float lyricTop = contentRect.top + 30.0f;
            float lyricWidth = (contentRect.right - 20.0f) - lyricLeft;
            D2D1_RECT_F lyricRect = D2D1::RectF(lyricLeft, lyricTop, lyricLeft + lyricWidth, lyricTop + 25.0f);
            m_lyricsComponent->Draw(lyricRect, ctx.contentAlpha, ctx.currentTimeMs);
        }
        break;
    case IslandDisplayMode::Idle:
    default:
        if (ctx.islandHeight < 35.0f) {
            break;
        }
        if (m_clockComponent) m_clockComponent->Draw(contentRect, ctx.contentAlpha, ctx.currentTimeMs);
        if (m_weatherComponent) {
            float iconSize = (contentRect.bottom - contentRect.top) * 0.4f;
            float iconX = contentRect.right - iconSize - 15.0f;
            float iconY = contentRect.top + ((contentRect.bottom - contentRect.top) - iconSize) / 2.0f;
            m_weatherComponent->DrawCompact(iconX, iconY, iconSize, ctx.contentAlpha, ctx.currentTimeMs);
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
    float secRight = secLeft + secWidth;
    float secBottom = secTop + ctx.secondaryHeight;
    float secRadius = (ctx.secondaryHeight < 60.0f) ? (ctx.secondaryHeight / 2.0f) : 20.0f;

    D2D1_ROUNDED_RECT secRect = D2D1::RoundedRect(D2D1::RectF(secLeft, secTop, secRight, secBottom), secRadius, secRadius);
    float shellAlpha = (ctx.secondaryContent == SecondaryContentKind::Volume) ? ctx.secondaryAlpha : 1.0f;
    m_blackBrush->SetOpacity(shellAlpha);
    m_d2dContext->FillRoundedRectangle(&secRect, m_blackBrush.Get());
    m_activeSecondaryComponent = ResolveSecondaryComponent(ctx.secondaryContent);
    m_lastSecondaryRect = secRect.rect;
    if (ctx.secondaryContent == SecondaryContentKind::Volume && m_volumeComponent) {
        m_volumeComponent->DrawSecondary(secLeft, secTop, secWidth, ctx.secondaryHeight, ctx.secondaryAlpha);
    } else if (m_fileStorageComponent) {
        m_fileStorageComponent->Draw(secRect.rect, ctx.secondaryAlpha, ctx.currentTimeMs);
    }
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

    float left = (ctx.canvasWidth - ctx.islandWidth) / 2.0f;
    float top = 10.0f;
    float right = left + ctx.islandWidth;
    float bottom = top + ctx.islandHeight;

    m_targetRadius = (ctx.islandHeight < 60.0f) ? (ctx.islandHeight / 2.0f) : 20.0f;
    m_currentRadius += (m_targetRadius - m_currentRadius) * 0.4f;
    float radius = m_currentRadius;

    D2D1_ROUNDED_RECT capsuleRect = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), radius, radius);
    m_blackBrush->SetOpacity(1.0f);
    m_d2dContext->FillRoundedRectangle(&capsuleRect, m_blackBrush.Get());

    if (ctx.contentAlpha > 0.01f) {
        ComPtr<ID2D1RoundedRectangleGeometry> clipGeometry;
        m_d2dFactory->CreateRoundedRectangleGeometry(&capsuleRect, &clipGeometry);
        ComPtr<ID2D1Layer> layer;
        m_d2dContext->CreateLayer(&layer);
        m_d2dContext->PushLayer(D2D1::LayerParameters(
            D2D1::InfiniteRect(), clipGeometry.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE), layer.Get());

        m_whiteBrush->SetOpacity(ctx.contentAlpha);
        m_grayBrush->SetOpacity(ctx.contentAlpha);
        m_themeBrush->SetOpacity(ctx.contentAlpha);

        DrawPrimaryContent(D2D1::RectF(left, top, right, bottom), ctx);
        m_d2dContext->PopLayer();
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
        (m_weatherComponent && m_weatherComponent->NeedsRender());
}
