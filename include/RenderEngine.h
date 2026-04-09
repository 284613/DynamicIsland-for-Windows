//RenderEngine.h
#pragma once
#include <windows.h>
#include <string>
#include "Constants.h"
#include <unordered_map>  // 新增：图标缓存
#include <memory>
#include <dcomp.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <wrl.h>
#include <wincodec.h>
#include "Messages.h" // 【加上这一行！】让画板认识 AlertInfo 结构体
#include "IslandState.h"
#include "components/IIslandComponent.h"
#include "components/WeatherComponent.h"
#include "components/WaveformComponent.h"
#include "components/LyricsComponent.h"
#include <vector>
#include <utility>
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")

// Forward declarations
class MusicPlayerComponent;
class AlertComponent;
class VolumeComponent;

using namespace Microsoft::WRL;

class RenderEngine {
public:
    RenderEngine();
    ~RenderEngine();

    bool IsScrolling() const { 
        return m_titleScrolling || m_artistScrolling || m_lyricScrolling; 
    }

    bool Initialize(HWND hwnd, int canvasWidth, int canvasHeight);
    void DrawCapsule(const RenderContext& ctx);
    void SetWeatherInfo(const std::wstring& desc, float temp) {
        m_weatherDesc = desc;
        m_weatherTemp = temp;
    }
    bool LoadAlbumArt(const std::wstring& file);
    bool LoadAlbumArtFromMemory(const std::vector<uint8_t>& data); // 【新增】从内存加载专辑封面
    // 新增：加载通知图标


    void UpdateScroll(float deltaTime, float audioLevel, float islandHeight, const struct LyricData& lyric);
    void SetAlertState(int alert) { m_currentAlertAlert = alert; }
    static constexpr float BUTTON_AREA_WIDTH = Constants::UI::BUTTON_SIZE * 3 + Constants::UI::BUTTON_SPACING * 2;
    void SetPlaybackButtonStates(int hoveredIndex, int pressedIndex) {
        m_hoveredButtonIndex = hoveredIndex;
        m_pressedButtonIndex = pressedIndex;
    }
    void SetAlertState(bool active, const AlertInfo& info) {
        m_isAlertActive = active;
        m_currentAlert = info;
    }
    void SetProgressBarStates(int hovered, int pressed) {
        m_hoveredProgress = hovered;
        m_pressedProgress = pressed;
    }
    bool LoadAlertIcon(const std::wstring& file); // 【新增】加载通知图标
    bool LoadAlertIconFromMemory(const std::vector<uint8_t>& data); // 【新增】从内存加载通知图标
    void SetDpi(float dpi);
    void Resize(int width, int height); // 应对窗口物理大小的改变
    void TriggerWeatherAnimOnce() { m_weatherAnimPhase = 0.0f; }

private:
    void DrawWeatherExpanded(const RenderContext& ctx, float left, float top, float right, float bottom, float islandWidth, float islandHeight);
    void DrawWeatherDaily(const RenderContext& ctx, float left, float top, float right, float bottom, float islandWidth, float islandHeight);
    void DrawWeatherAmbientBg(float L, float T, float R, float B, float alpha, ULONGLONG currentTime);
    // 新增：绘制播放控制按钮（仅在展开模式且有媒体会话时显示）
    void DrawPlaybackButtons(float left, float top, float buttonSize, float contentAlpha, bool isPlaying);

    // Component instances for decoupled rendering (legacy, will be migrated)
    std::unique_ptr<MusicPlayerComponent> m_musicComponent;
    std::unique_ptr<AlertComponent> m_alertComponent;
    std::unique_ptr<VolumeComponent> m_volumeComponent;

    // PR2: 天气组件（已组件化）
    std::unique_ptr<WeatherComponent> m_weatherComponent;
    // PR3: 歌词和波形组件（已组件化）
    std::unique_ptr<LyricsComponent>  m_lyricsComponent;
    std::unique_ptr<WaveformComponent> m_waveformComponent;

    // 组件注册表（PR1 骨架，后续各 PR 逐步填充）
    void RegisterComponents();
    SharedResources m_sharedRes;
    // 优先级有序的组件列表：{DisplayMode, component}
    // PR2+ 开始逐步往这里注册组件
    std::vector<std::pair<IslandDisplayMode, IIslandComponent*>> m_componentStack;

private:
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

    ComPtr<ID2D1SolidColorBrush> m_blackBrush;
    ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;
    ComPtr<ID2D1SolidColorBrush> m_themeBrush;

    // 【新增】预创建的动态颜色画刷（避免每帧创建）
    ComPtr<ID2D1SolidColorBrush> m_wifiBrush;      // WiFi 绿色
    ComPtr<ID2D1SolidColorBrush> m_bluetoothBrush;  // 蓝牙 蓝色
    ComPtr<ID2D1SolidColorBrush> m_chargingBrush;  // 充电 亮绿色
    ComPtr<ID2D1SolidColorBrush> m_lowBatteryBrush; // 低电量 红色
    ComPtr<ID2D1SolidColorBrush> m_fileBrush;       // 文件暂存 天蓝色
    ComPtr<ID2D1SolidColorBrush> m_notificationBrush; // 通知 橙红色
    ComPtr<ID2D1SolidColorBrush> m_darkGrayBrush;  // 深灰色背景
    ComPtr<ID2D1SolidColorBrush> m_progressBgBrush; // 进度条背景
    ComPtr<ID2D1SolidColorBrush> m_progressFgBrush; // 进度条前景
    ComPtr<ID2D1SolidColorBrush> m_buttonHoverBrush; // 按钮悬浮背景

    // 【新增】歌词遮罩渐变画刷
    ComPtr<ID2D1LinearGradientBrush> m_lyricFadeLeftBrush;
    ComPtr<ID2D1LinearGradientBrush> m_lyricFadeRightBrush;
    enum class WeatherType { Clear, PartlyCloudy, Cloudy, Rainy, Thunder, Snow, Fog, Default };
    WeatherType m_weatherType = WeatherType::Default;
    float m_weatherAnimPhase = 0.0f;
    ULONGLONG m_lastWeatherAnimTime = 0;

    void DrawWeatherIcon(float x, float y, float size, float alpha, ULONGLONG currentTime);
    WeatherType MapWeatherDescToType(const std::wstring& desc) const;

    ComPtr<IWICImagingFactory> m_wicFactory;
    ComPtr<ID2D1Bitmap> m_albumBitmap;
    ComPtr<ID2D1Bitmap1> m_targetBitmap;
    ComPtr<IDWriteTextFormat> m_iconTextFormat;   // 用于 Segoe MDL2 Assets 图标

    ComPtr<ID2D1Bitmap> m_alertBitmap;            // 【新增】保存加载到的通知图标

    // 【新增】TextLayout 缓存结构
    struct TextLayoutCacheEntry {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        float maxWidth;
    };
    std::unordered_map<std::wstring, TextLayoutCacheEntry> m_textLayoutCache;

    // 【新增】获取或创建 TextLayout（带缓存）
    ComPtr<IDWriteTextLayout> GetOrCreateTextLayout(
        const std::wstring& text,
        IDWriteTextFormat* format,
        float maxWidth,
        const std::wstring& cacheKey);

private:
    // 滚动偏移（像素）
    float m_titleScrollOffset = 0.0f;
    float m_artistScrollOffset = 0.0f;

    // 上次更新时间（用于计算时间差）
    ULONGLONG m_lastScrollTime = 0;

    // 是否正在滚动（避免不必要的计算）
    bool m_titleScrolling = false;
    bool m_artistScrolling = false;
    std::wstring m_lastDrawnFullText;  // 用于紧凑模式滚动检测

    // 上一次绘制的标题和艺术家（用于判断文本是否变化，变化时重置偏移）
    std::wstring m_lastDrawnTitle;
    std::wstring m_lastDrawnArtist;



    float m_currentWaveHeight[3] = { 10.0f, 10.0f, 10.0f };  // 当前高度
    float m_targetWaveHeight[3] = { 10.0f, 10.0f, 10.0f };  // 目标高度
    ULONGLONG m_lastRandomUpdate = 0;                       // 上次随机更新时间
    float m_wavePhase[3] = { 0.0f, 1.2f, 2.4f }; // 三根柱子的初始相位（错开）
    int m_hoveredButtonIndex = -1;
    int m_pressedButtonIndex = -1;
    int m_hoveredProgress = -1;
    int m_pressedProgress = -1;
    int m_currentAlertAlert = 0; // 当前要显示的提示类型（0=无, 1=WiFi, 2=蓝牙）
    bool m_isAlertActive = false;
    AlertInfo m_currentAlert;
    std::wstring m_lastDrawnLyric;      // 上次绘制的歌词，用于检测变化重置滚动
    float m_lyricScrollOffset = 0.0f;    // 歌词滚动偏移量
    float m_lyricScrollVelocity = 0.0f;    // spring velocity for smooth scroll
    float m_lastLyricDuration = 0.0f;     // last known lyric duration for deceleration
    bool m_lyricScrolling = false;       // 当前歌词是否需要滚动
    float m_dpi = 96.0f;  // 【新增】保存当前的 DPI 值
    float m_lastIslandHeight = 0.0f;  // 上次绘制时的岛屿高度

    // 【新增】圆角Radius平滑插值
    float m_currentRadius = 14.0f;  // 当前圆角值（用于平滑过渡）
    float m_targetRadius = 14.0f;  // 目标圆角值
    // 天气数据
    std::wstring m_weatherDesc = L"Sunny";
    float m_weatherTemp = 25.0f;
};



