#pragma once
#include "IIslandComponent.h"
#include "IslandState.h"
#include <string>

class LyricsComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return !m_lyric.text.empty(); }
    bool NeedsRender() const override { return m_scrolling; }

    void SetLyric(const LyricData& lyric) { m_lyric = lyric; }
    void ResetScroll();

    // 供 MusicPlayerComponent 查询滚动偏移
    float GetScrollOffset() const { return m_scrollOffset; }
    bool  IsScrolling()     const { return m_scrolling; }

private:
    SharedResources* m_res = nullptr;

    LyricData m_lyric;
    float     m_scrollOffset   = 0.0f;
    float     m_scrollVelocity = 0.0f;
    bool      m_scrolling      = false;
    std::wstring m_lastLyric;

    // 渐变遮罩画刷（左右边缘淡出）
    ComPtr<ID2D1LinearGradientBrush> m_fadeLeft;
    ComPtr<ID2D1LinearGradientBrush> m_fadeRight;
};
