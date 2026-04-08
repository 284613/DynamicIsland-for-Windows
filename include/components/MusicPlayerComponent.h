// MusicPlayerComponent.h
#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <functional>
#include "IslandState.h"
#include "Messages.h"

using namespace Microsoft::WRL;

class MusicPlayerComponent {
public:
    MusicPlayerComponent();
    ~MusicPlayerComponent();

    bool Initialize();

    // Set D2D resources
    void SetD2DResources(
        ComPtr<ID2D1DeviceContext> d2dContext,
        ComPtr<IDWriteFactory> dwriteFactory,
        ComPtr<ID2D1Factory1> d2dFactory);

    // Set text formats
    void SetTextFormats(
        ComPtr<IDWriteTextFormat> titleFormat,
        ComPtr<IDWriteTextFormat> subFormat,
        ComPtr<IDWriteTextFormat> iconFormat);

    // Set brushes
    void SetBrushes(
        ComPtr<ID2D1SolidColorBrush> whiteBrush,
        ComPtr<ID2D1SolidColorBrush> grayBrush,
        ComPtr<ID2D1SolidColorBrush> themeBrush,
        ComPtr<ID2D1SolidColorBrush> progressBgBrush,
        ComPtr<ID2D1SolidColorBrush> progressFgBrush,
        ComPtr<ID2D1SolidColorBrush> buttonHoverBrush);

    // Set album bitmap
    void SetAlbumBitmap(ComPtr<ID2D1Bitmap> bitmap);

    // Set WIC factory
    void SetWicFactory(ComPtr<IWICImagingFactory> wicFactory);

    // Set scroll state from RenderEngine
    void SetScrollState(float titleScrollOffset, float lyricScrollOffset, bool titleScrolling, bool lyricScrolling);

    // Main Draw method - handles all music player rendering
    void Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
              const RenderContext& ctx_data, float dpi);

    // Load album art
    bool LoadAlbumArt(const std::wstring& file);
    bool LoadAlbumArtFromMemory(const std::vector<uint8_t>* data, size_t size);

private:
    // Internal rendering methods
    void RenderExpanded(float left, float top, float width, float height, const RenderContext& ctx_data);
    void RenderCompact(float left, float top, float width, float height, const RenderContext& ctx_data);
    void RenderAlbumArt(float left, float top, float size, float alpha);
    void RenderProgressBar(float left, float top, float width, float height,
        float progress, float alpha, int hoveredProgress, int pressedProgress);
    void RenderPlaybackButtons(float left, float top, float buttonSize, float alpha, bool isPlaying,
        int hoveredButton, int pressedButton);
    void RenderLyrics(float left, float top, float width, const std::wstring& lyric, float alpha);
    void RenderCompactText(const std::wstring& fullText, float left, float top,
        float textRight, float islandHeight, float alpha);
    void RenderCompactTime(const std::wstring& timeText, float left, float top,
        float islandWidth, float islandHeight, float alpha);

    // D2D resources
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<IWICImagingFactory> m_wicFactory;

    // Album art bitmap
    ComPtr<ID2D1Bitmap> m_albumBitmap;

    // Text formats
    ComPtr<IDWriteTextFormat> m_titleFormat;
    ComPtr<IDWriteTextFormat> m_subFormat;
    ComPtr<IDWriteTextFormat> m_iconFormat;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;
    ComPtr<ID2D1SolidColorBrush> m_themeBrush;
    ComPtr<ID2D1SolidColorBrush> m_progressBgBrush;
    ComPtr<ID2D1SolidColorBrush> m_progressFgBrush;
    ComPtr<ID2D1SolidColorBrush> m_buttonHoverBrush;

    // Scroll state (kept in sync with RenderEngine)
    float m_titleScrollOffset = 0.0f;
    float m_lyricScrollOffset = 0.0f;
    bool m_titleScrolling = false;
    bool m_lyricScrolling = false;
    std::wstring m_lastDrawnFullText;
    std::wstring m_lastDrawnLyric;

    // Constants
    static constexpr float SCROLL_SPEED = 30.0f;
    static constexpr float BUTTON_SIZE = 30.0f;
    static constexpr float BUTTON_SPACING = 5.0f;
    static constexpr float ALBUM_ART_SIZE = 60.0f;
    static constexpr float ALBUM_ART_MARGIN = 20.0f;
    static constexpr float COMPACT_THRESHOLD = 60.0f;
};