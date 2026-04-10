#pragma once
#include "IIslandComponent.h"
#include <string>
#include <vector>

class MusicPlayerComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_hasSession; }
    bool NeedsRender() const override { return m_isCompact && m_titleScrolling; }
    void SetCompactMode(bool isCompact) { m_isCompact = isCompact; }

    void SetPlaybackState(bool hasSession, bool isPlaying, float progress,
                          const std::wstring& title, const std::wstring& artist);
    void SetInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress);

    bool LoadAlbumArt(const std::wstring& file);
    bool LoadAlbumArtFromMemory(const std::vector<uint8_t>& data);

private:
    void RenderExpanded(float left, float top, float width, float height, float contentAlpha);
    void RenderCompact(float left, float top, float width, float height, float contentAlpha);
    void RenderAlbumArt(float left, float top, float size, float alpha);
    void RenderProgressBar(float left, float top, float width, float height, float alpha);
    void RenderPlaybackButtons(float left, float top, float buttonSize, float alpha);
    void RenderCompactText(const std::wstring& text, float left, float top, float textRight, float height, float alpha);

    SharedResources* m_res = nullptr;
    ComPtr<ID2D1Bitmap> m_albumBitmap;

    bool m_hasSession = false;
    bool m_isPlaying = false;
    bool m_isCompact = false;
    float m_progress = 0.0f;
    std::wstring m_title;
    std::wstring m_artist;

    int m_hoveredButton = -1;
    int m_pressedButton = -1;
    int m_hoveredProgress = -1;
    int m_pressedProgress = -1;

    float m_titleScrollOffset = 0.0f;
    bool m_titleScrolling = false;
    std::wstring m_lastDrawnFullText;

    static constexpr float COMPACT_THRESHOLD = 60.0f;
    static constexpr float BUTTON_SIZE = 30.0f;
    static constexpr float BUTTON_SPACING = 5.0f;
    static constexpr float ALBUM_ART_SIZE = 60.0f;
    static constexpr float ALBUM_ART_MARGIN = 20.0f;
};
