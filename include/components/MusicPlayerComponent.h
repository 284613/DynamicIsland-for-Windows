// MusicPlayerComponent.h
#pragma once
#include "IIslandComponent.h"
#include "IslandState.h"
#include <string>
#include <vector>

class MusicPlayerComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_hasSession || m_isPlaying; }

    // State setters — call before Draw each frame
    void SetPlaybackState(bool hasSession, bool isPlaying, float progress,
                          const std::wstring& title, const std::wstring& artist,
                          const LyricData& lyric, bool showTime, const std::wstring& timeText);
    void SetInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress);
    void SetScrollState(float titleScrollOffset, float lyricScrollOffset, bool titleScrolling, bool lyricScrolling);
    void SetWaveHeights(const float heights[3]) { m_waveH[0] = heights[0]; m_waveH[1] = heights[1]; m_waveH[2] = heights[2]; }
    void SetAlbumBitmap(ComPtr<ID2D1Bitmap> bitmap) { m_albumBitmap = bitmap; }

    bool LoadAlbumArtFromMemory(const std::vector<uint8_t>& data);

private:
    void RenderExpanded(float left, float top, float width, float height, float contentAlpha);
    void RenderCompact(float left, float top, float width, float height, float contentAlpha);
    void RenderAlbumArt(float left, float top, float size, float alpha);
    void RenderProgressBar(float left, float top, float width, float height, float alpha);
    void RenderPlaybackButtons(float left, float top, float buttonSize, float alpha);
    void RenderLyrics(float left, float top, float width, float alpha);
    void RenderCompactText(const std::wstring& text, float left, float top, float textRight, float height, float alpha);
    void RenderCompactTime(const std::wstring& timeText, float left, float top, float width, float height, float alpha);

    SharedResources* m_res = nullptr;
    ComPtr<ID2D1Bitmap> m_albumBitmap;

    // Playback state
    bool m_hasSession   = false;
    bool m_isPlaying    = false;
    float m_progress    = 0.0f;
    std::wstring m_title;
    std::wstring m_artist;
    LyricData    m_lyric;
    bool m_showTime = false;
    std::wstring m_timeText;

    // Wave heights
    float m_waveH[3] = { 10.0f, 10.0f, 10.0f };

    // Interaction state
    int m_hoveredButton   = -1;
    int m_pressedButton   = -1;
    int m_hoveredProgress = -1;
    int m_pressedProgress = -1;

    // Scroll state
    float m_titleScrollOffset = 0.0f;
    float m_lyricScrollOffset = 0.0f;
    bool  m_titleScrolling    = false;
    bool  m_lyricScrolling    = false;
    std::wstring m_lastDrawnFullText;

    static constexpr float COMPACT_THRESHOLD = 60.0f;
    static constexpr float BUTTON_SIZE       = 30.0f;
    static constexpr float BUTTON_SPACING    = 5.0f;
    static constexpr float ALBUM_ART_SIZE    = 60.0f;
    static constexpr float ALBUM_ART_MARGIN  = 20.0f;
};
