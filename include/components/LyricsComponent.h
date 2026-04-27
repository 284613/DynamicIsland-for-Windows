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
    bool NeedsRender() const override { return m_scrolling || m_expandedScrolling || !m_lyric.words.empty() || m_lineTransition < 0.999f; }

    void SetLyric(const LyricData& lyric);
    void SetTranslationMode(LyricTranslationMode mode) { m_translationMode = mode; }
    void DrawCompact(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs);
    void DrawThreeLines(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs);
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
    void DrawSingleLine(const std::wstring& text, IDWriteTextFormat* format, ID2D1Brush* brush,
                        const D2D1_RECT_F& rect, float alpha, bool center, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    void DrawScrollingLine(const std::wstring& text, const D2D1_RECT_F& rect, float alpha, ULONGLONG currentTimeMs);
    void DrawKaraokeLine(const D2D1_RECT_F& rect, float alpha);
    bool ShouldShowTranslation(bool currentLine) const;

    SharedResources* m_res = nullptr;

    LyricData m_lyric;
    LyricTranslationMode m_translationMode = LyricTranslationMode::CurrentOnly;
    float     m_scrollOffset   = 0.0f;
    float     m_scrollVelocity = 0.0f;
    bool      m_scrolling      = false;
    std::wstring m_lastLyric;
    int64_t m_lastCurrentMs = -1;
    float m_lineTransition = 1.0f;
    float m_expandedScrollOffset = 0.0f;
    bool m_expandedScrolling = false;

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
