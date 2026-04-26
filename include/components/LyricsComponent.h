#pragma once
#include "IIslandComponent.h"
#include "IslandState.h"
#include <string>
#include <vector>

class LyricsComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return !m_lyric.text.empty(); }
    bool NeedsRender() const override { return m_scrolling || !m_lyric.words.empty(); }

    void SetLyric(const LyricData& lyric);
    void DrawCompact(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs);
    void ResetScroll();

    // 供 MusicPlayerComponent 查询滚动偏移
    float GetScrollOffset() const { return m_scrollOffset; }
    bool  IsScrolling()     const { return m_scrolling; }

private:
    struct CompactWordSpan {
        float advance = 0.0f;
        int64_t startMs = -1;
        int64_t durationMs = 0;
    };

    void InvalidateCompactCache();
    bool EnsureCompactLayout(float maxWidth);

    SharedResources* m_res = nullptr;

    LyricData m_lyric;
    float     m_scrollOffset   = 0.0f;
    float     m_scrollVelocity = 0.0f;
    bool      m_scrolling      = false;
    std::wstring m_lastLyric;

    ComPtr<IDWriteTextLayout> m_compactLineLayout;
    std::vector<CompactWordSpan> m_compactWordSpans;
    std::wstring m_compactCacheSignature;
    float m_compactCacheMaxWidth = -1.0f;
    float m_compactCacheTextHeight = 0.0f;
    float m_compactCacheTextWidth = 0.0f;

    // 渐变遮罩画刷（左右边缘淡出）
    ComPtr<ID2D1LinearGradientBrush> m_fadeLeft;
    ComPtr<ID2D1LinearGradientBrush> m_fadeRight;
};
