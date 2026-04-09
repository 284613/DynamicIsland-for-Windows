#include "components/LyricsComponent.h"
#include "Constants.h"
#include <cmath>

void LyricsComponent::OnAttach(SharedResources* res) {
    m_res = res;
    if (!res) return;

    // 创建左侧渐变遮罩
    D2D1_GRADIENT_STOP stopsL[2] = {
        {0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)},
        {1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)}
    };
    ComPtr<ID2D1GradientStopCollection> collL;
    res->d2dContext->CreateGradientStopCollection(stopsL, 2, &collL);
    if (collL) {
        res->d2dContext->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(30, 0)),
            collL.Get(), &m_fadeLeft);
    }

    // 创建右侧渐变遮罩
    D2D1_GRADIENT_STOP stopsR[2] = {
        {0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)},
        {1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)}
    };
    ComPtr<ID2D1GradientStopCollection> collR;
    res->d2dContext->CreateGradientStopCollection(stopsR, 2, &collR);
    if (collR) {
        res->d2dContext->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(30, 0)),
            collR.Get(), &m_fadeRight);
    }
}

void LyricsComponent::ResetScroll() {
    m_scrollOffset   = 0.0f;
    m_scrollVelocity = 0.0f;
    m_scrolling      = false;
}

void LyricsComponent::Update(float deltaTime) {
    if (!m_scrolling) {
        m_scrollOffset   = 0.0f;
        m_scrollVelocity = 0.0f;
        return;
    }

    float scrollSpeed = Constants::Animation::SCROLL_SPEED;
    // 智能减速：最后 2 秒减慢
    if (m_lyric.nextMs > 0 && m_lyric.positionMs > 0) {
        float remaining = (float)(m_lyric.nextMs - m_lyric.positionMs) / 1000.0f;
        if (remaining > 0.0f && remaining < 2.0f) {
            float t = remaining / 2.0f;
            float ease = t * t * (3.0f - 2.0f * t);
            scrollSpeed *= ease;
        }
    }
    // 弹簧物理
    const float stiffness = 8.0f, damping = 0.5f;
    m_scrollVelocity += (scrollSpeed - m_scrollVelocity) * stiffness * 0.016f;
    m_scrollVelocity *= (1.0f - damping * 0.016f);
    m_scrollOffset   += m_scrollVelocity * deltaTime;
}

void LyricsComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || m_lyric.text.empty()) return;

    auto* ctx = m_res->d2dContext;
    auto* wb  = m_res->whiteBrush;
    const std::wstring& text = m_lyric.text;
    float maxWidth = rect.right - rect.left;

    // 歌词变化时重置滚动
    if (text != m_lastLyric) {
        m_lastLyric      = text;
        m_scrollOffset   = 0.0f;
        m_scrollVelocity = 0.0f;
    }

    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(
        text.c_str(), (UINT32)text.size(),
        m_res->titleFormat, maxWidth, 10000.f, &layout);
    if (!layout) return;

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    bool needScroll = (metrics.width > maxWidth);
    m_scrolling = needScroll;

    ctx->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    wb->SetOpacity(contentAlpha);

    if (needScroll) {
        float offset = fmodf(m_scrollOffset, metrics.width);
        ctx->DrawTextLayout(D2D1::Point2F(rect.left - offset,               rect.top), layout.Get(), wb);
        ctx->DrawTextLayout(D2D1::Point2F(rect.left - offset + metrics.width, rect.top), layout.Get(), wb);
    } else {
        ctx->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), wb);
    }

    ctx->PopAxisAlignedClip();
}
