#pragma once
#include <windows.h>
#include <string>
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <dcomp.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <wrl.h>
#include <wincodec.h>
#include "Messages.h"
#include "IslandState.h"
#include "AgentSessionModel.h"
#include "components/IIslandComponent.h"
#include "components/AgentSessionsComponent.h"
#include "components/WeatherComponent.h"
#include "components/WaveformComponent.h"
#include "components/LyricsComponent.h"
#include "components/MusicPlayerComponent.h"
#include "components/AlertComponent.h"
#include "components/VolumeComponent.h"
#include "components/FilePanelComponent.h"
#include "components/ClockComponent.h"
#include "components/PomodoroComponent.h"
#include "components/TodoComponent.h"

#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")

using namespace Microsoft::WRL;

class RenderEngine {
public:
    RenderEngine();
    ~RenderEngine();

    bool Initialize(HWND hwnd, int canvasWidth, int canvasHeight);
    void DrawCapsule(const RenderContext& ctx);

    void SetDpi(float dpi);
    void Resize(int width, int height);

    bool OnMouseWheel(float x, float y, int delta);
    bool OnMouseMove(float x, float y);
    bool OnMouseClick(float x, float y);
    bool OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM key);
    bool OnImeComposition(HWND hwnd, LPARAM lParam);
    bool OnImeSetContext(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result);
    D2D1_RECT_F GetImeAnchorRect() const;
    bool SecondaryContainsPoint(float x, float y) const;
    FilePanelComponent::HitResult HitTestFileSecondary(float x, float y) const;
    bool HandleIdleTodoMouseMove(float x, float y);
    bool IdleTodoBadgeContains(float x, float y) const;

    bool HasActiveAnimations() const;

    void SetPlaybackState(bool hasSession, bool isPlaying, float progress,
                          const std::wstring& title, const std::wstring& artist);
    void SetMusicArtworkStyles(MusicArtworkStyle compactStyle, MusicArtworkStyle expandedStyle);
    void SetMusicInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress);
    void SetLyricData(const LyricData& lyric);
    void SetWaveformState(const std::array<float, 3>& bandLevels, float islandHeight, WaveformDisplayStyle style);
    void SetTimeData(bool showTime, const std::wstring& timeText);
    void SetVolumeState(bool active, float volumeLevel);
    void SetFileState(SecondaryContentKind mode, const std::vector<FileStashItem>& storedFiles, int selectedIndex, int hoveredIndex);
    void SetWeatherState(const std::wstring& locationText, const std::wstring& desc, float temp, const std::wstring& iconId,
                         const std::vector<HourlyForecast>& hourly, const std::vector<DailyForecast>& daily,
                         bool expanded, WeatherViewMode viewMode, bool available);
    void SetAlertState(bool active, const AlertInfo& info);
    void SetTheme(bool darkMode, float primaryOpacity, float secondaryOpacity);
    void SetClockClickCallback(std::function<void()> callback);
    void SetTodoStore(TodoStore* store);
    void SetAgentSessionState(const std::vector<AgentSessionSummary>& summaries,
        AgentSessionFilter filter,
        AgentKind selectedKind,
        const std::wstring& selectedSessionId,
        const std::vector<AgentHistoryEntry>& selectedHistory,
        AgentKind compactProvider,
        bool chooserOpen);
    PomodoroComponent* GetPomodoroComponent() const { return m_pomodoroComponent.get(); }
    TodoComponent* GetTodoComponent() const { return m_todoComponent.get(); }
    AgentSessionsComponent* GetAgentSessionsComponent() const { return m_agentSessionsComponent.get(); }

    bool LoadAlbumArt(const std::wstring& file);
    bool LoadAlbumArtFromMemory(const std::vector<uint8_t>& data);
    bool LoadAlertIcon(const std::wstring& file);
    bool LoadAlertIconFromMemory(const std::vector<uint8_t>& data);

private:
    void RegisterComponents();
    void UpdateComponents(float deltaTime, const RenderContext& ctx);
    IIslandComponent* ResolvePrimaryComponent(IslandDisplayMode mode);
    void DrawPrimaryContent(const D2D1_RECT_F& contentRect, const RenderContext& ctx);
    void DrawSecondaryIsland(const RenderContext& ctx, float top, float bottom);
    IIslandComponent* ResolveSecondaryComponent(SecondaryContentKind kind);
    D2D1_RECT_F BuildShrinkInterpolatedRect(const RenderContext& ctx, float top) const;
    void DrawShrinkHandle(const D2D1_RECT_F& rect, float alpha);
    void DrawCompactShrinkHandle(const D2D1_RECT_F& rect, float alpha);
    void DrawShrunkGhostHud(const RenderContext& ctx, float alpha);
    bool TryGetGhostHudText(IslandDisplayMode sourceMode, std::wstring& primary, std::wstring& secondary, bool& musicActive) const;

    SharedResources m_sharedRes;

    std::unique_ptr<MusicPlayerComponent> m_musicComponent;
    std::unique_ptr<AlertComponent> m_alertComponent;
    std::unique_ptr<VolumeComponent> m_volumeComponent;
    std::unique_ptr<FilePanelComponent> m_fileStorageComponent;
    std::unique_ptr<ClockComponent> m_clockComponent;
    std::unique_ptr<PomodoroComponent> m_pomodoroComponent;
    std::unique_ptr<TodoComponent> m_todoComponent;
    std::unique_ptr<AgentSessionsComponent> m_agentSessionsComponent;
    std::unique_ptr<WeatherComponent> m_weatherComponent;
    std::unique_ptr<LyricsComponent> m_lyricsComponent;
    std::unique_ptr<WaveformComponent> m_waveformComponent;

    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<IDXGIDevice> m_dxgiDevice;
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2dContext;

    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_rootVisual;
    ComPtr<IDCompositionSurface> m_surface;

    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_textFormatTitle;
    ComPtr<IDWriteTextFormat> m_textFormatSub;
    ComPtr<IDWriteTextFormat> m_iconTextFormat;

    ComPtr<ID2D1SolidColorBrush> m_blackBrush;
    ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;
    ComPtr<ID2D1SolidColorBrush> m_themeBrush;
    ComPtr<ID2D1SolidColorBrush> m_wifiBrush;
    ComPtr<ID2D1SolidColorBrush> m_bluetoothBrush;
    ComPtr<ID2D1SolidColorBrush> m_chargingBrush;
    ComPtr<ID2D1SolidColorBrush> m_lowBatteryBrush;
    ComPtr<ID2D1SolidColorBrush> m_fileBrush;
    ComPtr<ID2D1SolidColorBrush> m_notificationBrush;
    ComPtr<ID2D1SolidColorBrush> m_darkGrayBrush;
    ComPtr<ID2D1SolidColorBrush> m_progressBgBrush;
    ComPtr<ID2D1SolidColorBrush> m_progressFgBrush;
    ComPtr<ID2D1SolidColorBrush> m_buttonHoverBrush;

    ComPtr<IWICImagingFactory> m_wicFactory;

    float m_dpi = 96.0f;
    ULONGLONG m_lastFrameTime = 0;
    bool m_darkMode = true;
    float m_primaryShellOpacity = 1.0f;
    float m_secondaryShellOpacity = 1.0f;

    bool m_hasPlaybackSession = false;
    bool m_isPlaybackActive = false;
    std::wstring m_playbackTitle;
    std::wstring m_playbackArtist;
    bool m_showTime = false;
    std::wstring m_timeText;
    std::wstring m_weatherLocationText;
    std::wstring m_weatherDesc;
    std::wstring m_weatherIconId;
    float m_weatherTemp = 0.0f;
    bool m_weatherAvailable = false;
    std::vector<AgentSessionSummary> m_agentSummaries;
    AgentKind m_compactAgentProvider = AgentKind::Claude;
    TodoStore* m_todoStore = nullptr;

    IslandDisplayMode m_lastMode = IslandDisplayMode::Idle;
    IIslandComponent* m_activePrimaryComponent = nullptr;
    IIslandComponent* m_activeSecondaryComponent = nullptr;
    D2D1_RECT_F m_lastSecondaryRect = D2D1::RectF(0, 0, 0, 0);
};
