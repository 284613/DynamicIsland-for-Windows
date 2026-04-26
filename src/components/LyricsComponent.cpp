#include "components/LyricsComponent.h"
#include "Constants.h"
#include <algorithm>
#include <cmath>

namespace {
std::wstring BuildCompactLyricSignature(const LyricData& lyric) {
    std::wstring signature = lyric.text;
    signature.push_back(L'\x1f');
    for (const auto& word : lyric.words) {
        signature += word.text;
        signature.push_back(L'\x1e');
        signature += std::to_wstring(word.startMs);
        signature.push_back(L':');
        signature += std::to_wstring(word.durationMs);
        signature.push_back(L':');
        signature += std::to_wstring(word.flag);
        signature.push_back(L'\x1f');
    }
    return signature;
}
}

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

void LyricsComponent::SetLyric(const LyricData& lyric) {
    const std::wstring nextSignature = BuildCompactLyricSignature(lyric);
    if (nextSignature != m_compactCacheSignature) {
        InvalidateCompactCache();
        m_compactCacheSignature = nextSignature;
    }
    m_lyric = lyric;
}

void LyricsComponent::InvalidateCompactCache() {
    m_compactLineLayout.Reset();
    m_compactWordSpans.clear();
    m_compactCacheMaxWidth = -1.0f;
    m_compactCacheTextHeight = 0.0f;
    m_compactCacheTextWidth = 0.0f;
    m_scrollOffset = 0.0f;
    m_scrollVelocity = 0.0f;
}

bool LyricsComponent::EnsureCompactLayout(float maxWidth) {
    if (!m_res || !m_res->dwriteFactory || !m_res->subFormat || m_lyric.text.empty() || maxWidth <= 1.0f) {
        return false;
    }

    if (m_compactLineLayout && std::fabs(m_compactCacheMaxWidth - maxWidth) < 0.5f) {
        return true;
    }

    InvalidateCompactCache();
    m_res->dwriteFactory->CreateTextLayout(
        m_lyric.text.c_str(), (UINT32)m_lyric.text.size(),
        m_res->subFormat, 10000.0f, 40.0f, &m_compactLineLayout);
    if (!m_compactLineLayout) {
        return false;
    }

    m_compactLineLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_compactLineLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    DWRITE_TEXT_METRICS lineMetrics{};
    m_compactLineLayout->GetMetrics(&lineMetrics);
    m_compactCacheTextHeight = lineMetrics.height;
    m_compactCacheTextWidth = (std::max)(lineMetrics.widthIncludingTrailingWhitespace, lineMetrics.width);

    m_compactWordSpans.reserve(m_lyric.words.size());
    for (const auto& word : m_lyric.words) {
        if (word.text.empty()) {
            continue;
        }

        ComPtr<IDWriteTextLayout> wordLayout;
        m_res->dwriteFactory->CreateTextLayout(
            word.text.c_str(), (UINT32)word.text.size(),
            m_res->subFormat, 10000.0f, 40.0f, &wordLayout);
        if (!wordLayout) {
            continue;
        }

        wordLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS wordMetrics{};
        wordLayout->GetMetrics(&wordMetrics);

        CompactWordSpan span;
        span.advance = (std::max)(wordMetrics.widthIncludingTrailingWhitespace, wordMetrics.width);
        span.startMs = word.startMs;
        span.durationMs = (std::max)(int64_t{ 1 }, word.durationMs);
        m_compactWordSpans.push_back(span);
    }

    m_compactCacheMaxWidth = maxWidth;
    return true;
}

void LyricsComponent::ResetScroll() {
    m_scrollOffset   = 0.0f;
    m_scrollVelocity = 0.0f;
    m_scrolling      = false;
}

void LyricsComponent::Update(float deltaTime) {
    if (!m_scrolling) {
        m_scrollVelocity = 0.0f;
        return;
    }

    // 智能减速：最后 2 秒减慢
    // 弹簧物理
    m_scrollVelocity *= std::pow(0.30f, (std::max)(0.0f, deltaTime));
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

void LyricsComponent::DrawCompact(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || m_lyric.text.empty() || contentAlpha <= 0.01f) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    const float maxWidth = rect.right - rect.left;
    if (!ctx || maxWidth <= 1.0f || !m_res->subFormat) {
        return;
    }

    ctx->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_lyric.words.empty()) {
        m_scrolling = false;
        m_scrollOffset = 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        m_res->dwriteFactory->CreateTextLayout(
            m_lyric.text.c_str(), (UINT32)m_lyric.text.size(),
            m_res->subFormat, maxWidth, 40.0f, &layout);
        if (layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TEXT_METRICS metrics{};
            layout->GetMetrics(&metrics);
            const float y = rect.top + (rect.bottom - rect.top - metrics.height) * 0.5f;
            m_res->grayBrush->SetOpacity(0.65f * contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(rect.left, y), layout.Get(), m_res->grayBrush);
        }
        ctx->PopAxisAlignedClip();
        return;
    }

    if (!EnsureCompactLayout(maxWidth)) {
        ctx->PopAxisAlignedClip();
        return;
    }
    const float y = rect.top + (rect.bottom - rect.top - m_compactCacheTextHeight) * 0.5f;
    float highlightWidth = 0.0f;
    const int64_t positionMs = m_lyric.positionMs;
    for (const auto& span : m_compactWordSpans) {
        if (highlightWidth >= m_compactCacheTextWidth) {
            continue;
        }

        float progress = 0.0f;
        if (positionMs >= span.startMs + span.durationMs) {
            progress = 1.0f;
        } else if (positionMs > span.startMs) {
            progress = static_cast<float>(positionMs - span.startMs) / static_cast<float>(span.durationMs);
        }
        progress = (std::max)(0.0f, (std::min)(1.0f, progress));

        highlightWidth += span.advance * progress;
        if (progress < 1.0f) {
            break;
        }
    }

    const bool overflow = m_compactCacheTextWidth > maxWidth + 1.0f;
    m_scrolling = overflow && !m_compactWordSpans.empty();
    if (overflow) {
        const float maxOffset = (std::max)(0.0f, m_compactCacheTextWidth - maxWidth);
        const float targetOffset = (std::max)(0.0f, (std::min)(maxOffset, highlightWidth - maxWidth * 0.46f));
        m_scrollOffset += (targetOffset - m_scrollOffset) * 0.22f;
        m_scrollOffset = (std::max)(0.0f, (std::min)(maxOffset, m_scrollOffset));
    } else {
        m_scrollOffset += (0.0f - m_scrollOffset) * 0.22f;
    }

    const float originX = rect.left - m_scrollOffset;
    m_res->grayBrush->SetOpacity(0.42f * contentAlpha);
    ctx->DrawTextLayout(D2D1::Point2F(originX, y), m_compactLineLayout.Get(), m_res->grayBrush);

    if (highlightWidth > 0.0f) {
        auto drawHighlightAt = [&](float originX) {
            const float clippedHighlight = (std::min)(m_compactCacheTextWidth, highlightWidth);
            D2D1_RECT_F highlightClip = D2D1::RectF(originX, rect.top, originX + clippedHighlight, rect.bottom);
            highlightClip.left = (std::max)(highlightClip.left, rect.left);
            highlightClip.right = (std::min)(highlightClip.right, rect.right);
            if (highlightClip.right <= highlightClip.left) {
                return;
            }

            ctx->PushAxisAlignedClip(highlightClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (m_res->themeBrush) {
                m_res->themeBrush->SetOpacity(contentAlpha);
                ctx->DrawTextLayout(D2D1::Point2F(originX, y), m_compactLineLayout.Get(), m_res->themeBrush);
            } else {
                m_res->whiteBrush->SetOpacity(contentAlpha);
                ctx->DrawTextLayout(D2D1::Point2F(originX, y), m_compactLineLayout.Get(), m_res->whiteBrush);
            }
            ctx->PopAxisAlignedClip();
        };
        drawHighlightAt(originX);
    }

    ctx->PopAxisAlignedClip();
}
