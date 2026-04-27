#pragma once
#include "IIslandComponent.h"
#include "IslandState.h"
#include <string>
#include <vector>

class MusicPlayerComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_hasSession; }
    bool NeedsRender() const override { return m_isCompact && (m_titleScrolling || m_isPlaying); }
    void SetCompactMode(bool isCompact) { m_isCompact = isCompact; }
    void SetArtworkStyles(MusicArtworkStyle compactStyle, MusicArtworkStyle expandedStyle) {
        m_compactArtworkStyle = compactStyle;
        m_expandedArtworkStyle = expandedStyle;
    }
    void SetVinylRingPulse(bool enabled) { m_vinylRingPulse = enabled; }
    void SetLiked(bool liked) { m_liked = liked; }
    bool ShouldHideCompactWaveform() const { return m_isCompact && m_controlHoverAlpha > 0.03f; }

    void SetPlaybackState(bool hasSession, bool isPlaying, float progress,
                          const std::wstring& title, const std::wstring& artist,
                          int64_t positionMs = 0, int64_t durationMs = 0);
    void SetInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress);

    bool LoadAlbumArt(const std::wstring& file);
    bool LoadAlbumArtFromMemory(const std::vector<uint8_t>& data);

private:
    struct MusicVisualLayout {
        D2D1_RECT_F artRect = D2D1::RectF();
        D2D1_RECT_F titleRect = D2D1::RectF();
        D2D1_RECT_F artistRect = D2D1::RectF();
    };

    void RenderExpanded(float left, float top, float width, float height, float contentAlpha);
    void RenderCompact(float left, float top, float width, float height, float contentAlpha);
    MusicVisualLayout BuildVisualLayout(float left, float top, float width, float height, float expansion) const;
    void RenderAlbumArt(float left, float top, float size, float alpha);
    void RenderVinylRecord(float left, float top, float size, float alpha);
    void RenderVinylRings(float left, float top, float size, float alpha);
    void RenderProgressBar(float left, float top, float width, float height, float alpha);
    void RenderPlaybackButtons(float left, float top, float buttonSize, float alpha, bool expanded = false);
    void RenderPlaybackIcon(int iconKind, const D2D1_RECT_F& rect, ID2D1Brush* brush, float alpha, bool filled);
    void RenderCompactText(const std::wstring& text, float left, float top, float textRight, float height, float alpha);
    void RenderTextLine(const std::wstring& text, IDWriteTextFormat* format, ID2D1Brush* brush,
                        const D2D1_RECT_F& rect, float alpha, bool center);

    SharedResources* m_res = nullptr;
    ComPtr<ID2D1Bitmap> m_albumBitmap;

    bool m_hasSession = false;
    bool m_isPlaying = false;
    bool m_isCompact = false;
    float m_progress = 0.0f;
    int64_t m_positionMs = 0;
    int64_t m_durationMs = 0;
    std::wstring m_title;
    std::wstring m_artist;

    int m_hoveredButton = -1;
    int m_pressedButton = -1;
    int m_hoveredProgress = -1;
    int m_pressedProgress = -1;

    float m_titleScrollOffset = 0.0f;
    bool m_titleScrolling = false;
    std::wstring m_lastDrawnFullText;
    float m_vinylAngle = 0.0f;
    float m_ringPhase = 0.0f;
    float m_audioLevel = 0.0f;
    float m_controlHoverAlpha = 0.0f;
    bool m_vinylRingPulse = true;
    bool m_liked = false;
    MusicArtworkStyle m_compactArtworkStyle = MusicArtworkStyle::Vinyl;
    MusicArtworkStyle m_expandedArtworkStyle = MusicArtworkStyle::Square;

    static constexpr float COMPACT_THRESHOLD = 80.0f;
    static constexpr float BUTTON_SIZE = 30.0f;
    static constexpr float BUTTON_SPACING = 5.0f;
    static constexpr float ALBUM_ART_SIZE = 60.0f;
    static constexpr float ALBUM_ART_MARGIN = 20.0f;
};
