#include "components/LyricsComponent.h"
#include "Constants.h"
#include <algorithm>
#include <cmath>

namespace {
std::wstring BuildCompactLyricSignature(const LyricData& lyric) {
    std::wstring signature = lyric.text;
    signature.push_back(L'\x1d');
    signature += lyric.translation;
    signature.push_back(lyric.hasExplicitWordTiming ? L'\x1c' : L'\x1b');
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

std::wstring PreferredCurrentText(const LyricData& lyric, LyricTranslationMode mode) {
    if (mode == LyricTranslationMode::TranslationOnly && !lyric.translation.empty()) {
        return lyric.translation;
    }
    return lyric.text;
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
        m_expandedScrollOffset = 0.0f;
        m_expandedScrolling = false;
    }
    if (lyric.currentMs != m_lastCurrentMs) {
        m_lastCurrentMs = lyric.currentMs;
        m_lineTransition = 0.0f;
        m_expandedScrollOffset = 0.0f;
        m_expandedScrolling = false;
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
    const std::wstring displayText = PreferredCurrentText(m_lyric, m_translationMode);
    if (!m_res || !m_res->dwriteFactory || !m_res->subFormat || displayText.empty() || maxWidth <= 1.0f) {
        return false;
    }

    if (m_compactLineLayout && std::fabs(m_compactCacheMaxWidth - maxWidth) < 0.5f) {
        return true;
    }

    InvalidateCompactCache();
    m_res->dwriteFactory->CreateTextLayout(
        displayText.c_str(), (UINT32)displayText.size(),
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
    m_expandedScrollOffset = 0.0f;
    m_expandedScrolling = false;
}

void LyricsComponent::Update(float deltaTime) {
    if (m_lineTransition < 1.0f) {
        m_lineTransition = (std::min)(1.0f, m_lineTransition + (std::max)(0.0f, deltaTime) * 7.5f);
    }
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
    const std::wstring displayText = PreferredCurrentText(m_lyric, m_translationMode);
    if (!m_res || displayText.empty() || contentAlpha <= 0.01f) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    const float maxWidth = rect.right - rect.left;
    if (!ctx || maxWidth <= 1.0f || !m_res->subFormat) {
        return;
    }

    const bool showTranslation = ShouldShowTranslation(true) && rect.bottom - rect.top > 26.0f;
    const D2D1_RECT_F mainRect = showTranslation
        ? D2D1::RectF(rect.left, rect.top, rect.right, rect.top + (rect.bottom - rect.top) * 0.55f)
        : rect;
    const D2D1_RECT_F transRect = showTranslation
        ? D2D1::RectF(rect.left, mainRect.bottom + 1.0f, rect.right, rect.bottom)
        : D2D1::RectF();

    ctx->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const bool useWordTiming = m_translationMode != LyricTranslationMode::TranslationOnly && m_lyric.hasExplicitWordTiming;
    if (!useWordTiming || m_lyric.words.empty()) {
        m_scrolling = false;
        m_scrollOffset = 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        m_res->dwriteFactory->CreateTextLayout(
            displayText.c_str(), (UINT32)displayText.size(),
            m_res->subFormat, maxWidth, 40.0f, &layout);
        if (layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TEXT_METRICS metrics{};
            layout->GetMetrics(&metrics);
            const float y = mainRect.top + (mainRect.bottom - mainRect.top - metrics.height) * 0.5f;
            m_res->grayBrush->SetOpacity(0.65f * contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(rect.left, y), layout.Get(), m_res->grayBrush);
        }
        ctx->PopAxisAlignedClip();
        if (showTranslation) {
            DrawSingleLine(m_lyric.translation, m_res->subFormat, m_res->whiteBrush, transRect, 0.62f * contentAlpha, false);
        }
        return;
    }

    if (!EnsureCompactLayout(maxWidth)) {
        ctx->PopAxisAlignedClip();
        return;
    }
    const float y = mainRect.top + (mainRect.bottom - mainRect.top - m_compactCacheTextHeight) * 0.5f;
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
            m_res->whiteBrush->SetOpacity(contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(originX, y), m_compactLineLayout.Get(), m_res->whiteBrush);
            ctx->PopAxisAlignedClip();
        };
        drawHighlightAt(originX);
    }

    ctx->PopAxisAlignedClip();

    if (showTranslation) {
        DrawSingleLine(m_lyric.translation, m_res->subFormat, m_res->whiteBrush, transRect, 0.62f * contentAlpha, false);
    }
}

bool LyricsComponent::ShouldShowTranslation(bool currentLine) const {
    if (m_lyric.translation.empty()) {
        return false;
    }
    if (m_translationMode == LyricTranslationMode::Off ||
        m_translationMode == LyricTranslationMode::TranslationOnly) {
        return false;
    }
    return currentLine || m_translationMode == LyricTranslationMode::AllLines;
}

void LyricsComponent::DrawSingleLine(const std::wstring& text, IDWriteTextFormat* format, ID2D1Brush* brush,
    const D2D1_RECT_F& rect, float alpha, bool center, DWRITE_FONT_WEIGHT weight) {
    if (!m_res || !m_res->dwriteFactory || !m_res->d2dContext || !format || !brush ||
        text.empty() || rect.right <= rect.left || rect.bottom <= rect.top || alpha <= 0.01f) {
        return;
    }

    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
        format, rect.right - rect.left, rect.bottom - rect.top, &layout);
    if (!layout) {
        return;
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetFontWeight(weight, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(text.size()) });
    DWRITE_TRIMMING trim{};
    trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    ComPtr<IDWriteInlineObject> ellipsis;
    m_res->dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis);
    layout->SetTrimming(&trim, ellipsis.Get());

    brush->SetOpacity(alpha);
    m_res->d2dContext->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_res->d2dContext->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush);
    m_res->d2dContext->PopAxisAlignedClip();
}

void LyricsComponent::DrawScrollingLine(const std::wstring& text, const D2D1_RECT_F& rect, float alpha, ULONGLONG currentTimeMs) {
    if (!m_res || !m_res->dwriteFactory || !m_res->d2dContext || !m_res->titleFormat ||
        text.empty() || rect.right <= rect.left || rect.bottom <= rect.top || alpha <= 0.01f) {
        return;
    }

    const float availableWidth = rect.right - rect.left;
    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
        m_res->titleFormat, 10000.0f, rect.bottom - rect.top, &layout);
    if (!layout) {
        return;
    }

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(text.size()) });

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float textWidth = (std::max)(metrics.widthIncludingTrailingWhitespace, metrics.width);
    auto* ctx = m_res->d2dContext;

    ctx->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_res->whiteBrush->SetOpacity(alpha);

    if (textWidth > availableWidth + 1.0f) {
        m_expandedScrolling = true;
        const float gap = 40.0f;
        const float cycleWidth = textWidth + gap;
        double elapsedMs = static_cast<double>(currentTimeMs);
        if (m_lyric.currentMs >= 0 && m_lyric.positionMs >= m_lyric.currentMs) {
            elapsedMs = static_cast<double>(m_lyric.positionMs - m_lyric.currentMs);
        }
        const float offset = static_cast<float>(std::fmod(elapsedMs * 0.028, cycleWidth));
        const float originX = rect.left - offset;
        ctx->DrawTextLayout(D2D1::Point2F(originX, rect.top), layout.Get(), m_res->whiteBrush);
        ctx->DrawTextLayout(D2D1::Point2F(originX + cycleWidth, rect.top), layout.Get(), m_res->whiteBrush);
    } else {
        m_expandedScrolling = false;
        m_expandedScrollOffset = 0.0f;
        ctx->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), m_res->whiteBrush);
    }

    ctx->PopAxisAlignedClip();
}

void LyricsComponent::DrawKaraokeLine(const D2D1_RECT_F& rect, float alpha) {
    if (!m_res || !m_res->dwriteFactory || !m_res->d2dContext || !m_res->titleFormat ||
        m_lyric.text.empty() || rect.right <= rect.left || rect.bottom <= rect.top || alpha <= 0.01f) {
        return;
    }

    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(m_lyric.text.c_str(), static_cast<UINT32>(m_lyric.text.size()),
        m_res->titleFormat, 10000.0f, rect.bottom - rect.top, &layout);
    if (!layout) {
        return;
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(m_lyric.text.size()) });

    DWRITE_TEXT_METRICS textMetrics{};
    layout->GetMetrics(&textMetrics);
    const float availableWidth = rect.right - rect.left;
    const float textWidth = (std::max)(textMetrics.widthIncludingTrailingWhitespace, textMetrics.width);

    float highlightWidth = 0.0f;
    if (!m_lyric.words.empty()) {
        for (const auto& word : m_lyric.words) {
            if (word.text.empty()) {
                continue;
            }
            ComPtr<IDWriteTextLayout> wordLayout;
            m_res->dwriteFactory->CreateTextLayout(word.text.c_str(), static_cast<UINT32>(word.text.size()),
                m_res->titleFormat, 10000.0f, rect.bottom - rect.top, &wordLayout);
            if (!wordLayout) {
                continue;
            }
            wordLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            wordLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(word.text.size()) });
            DWRITE_TEXT_METRICS wordMetrics{};
            wordLayout->GetMetrics(&wordMetrics);
            const float advance = (std::max)(wordMetrics.widthIncludingTrailingWhitespace, wordMetrics.width);

            float progress = 0.0f;
            if (m_lyric.positionMs >= word.startMs + word.durationMs) {
                progress = 1.0f;
            } else if (m_lyric.positionMs > word.startMs) {
                progress = static_cast<float>(m_lyric.positionMs - word.startMs) /
                    static_cast<float>((std::max)(int64_t{ 1 }, word.durationMs));
            }
            progress = (std::max)(0.0f, (std::min)(1.0f, progress));
            highlightWidth += advance * progress;
            if (progress < 1.0f) {
                break;
            }
        }
    }

    const bool overflow = textWidth > availableWidth + 1.0f;
    if (overflow) {
        m_expandedScrolling = true;
        const float maxOffset = (std::max)(0.0f, textWidth - availableWidth);
        const float targetOffset = (std::max)(0.0f, (std::min)(maxOffset, highlightWidth - availableWidth * 0.46f));
        m_expandedScrollOffset += (targetOffset - m_expandedScrollOffset) * 0.22f;
        m_expandedScrollOffset = (std::max)(0.0f, (std::min)(maxOffset, m_expandedScrollOffset));
    } else {
        m_expandedScrolling = false;
        m_expandedScrollOffset = 0.0f;
    }

    const float originX = rect.left - m_expandedScrollOffset;
    auto* ctx = m_res->d2dContext;
    m_res->grayBrush->SetOpacity(0.38f * alpha);
    ctx->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ctx->DrawTextLayout(D2D1::Point2F(originX, rect.top), layout.Get(), m_res->grayBrush);

    if (highlightWidth > 0.0f) {
        D2D1_RECT_F clip = D2D1::RectF(originX, rect.top,
            originX + (std::min)(textWidth, highlightWidth), rect.bottom);
        clip.left = (std::max)(clip.left, rect.left);
        clip.right = (std::min)(clip.right, rect.right);
        if (clip.right > clip.left) {
            ctx->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            m_res->whiteBrush->SetOpacity(alpha);
            ctx->DrawTextLayout(D2D1::Point2F(originX, rect.top), layout.Get(), m_res->whiteBrush);
            ctx->PopAxisAlignedClip();
        }
    }
    ctx->PopAxisAlignedClip();
}

void LyricsComponent::DrawThreeLines(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res || contentAlpha <= 0.01f) {
        return;
    }

    const bool translationOnly = m_translationMode == LyricTranslationMode::TranslationOnly && !m_lyric.translation.empty();
    const std::wstring currentText = translationOnly ? m_lyric.translation : m_lyric.text;
    if (currentText.empty()) {
        return;
    }

    const float lineGap = 4.0f;
    const float rowH = 21.0f;
    const float transH = ShouldShowTranslation(true) ? 15.0f : 0.0f;
    const float totalH = rowH * 3.0f + lineGap * 2.0f + transH;
    const float t = m_lineTransition * m_lineTransition * (3.0f - 2.0f * m_lineTransition);
    const float lineAlpha = contentAlpha * (0.55f + 0.45f * t);
    float y = rect.top + (rect.bottom - rect.top - totalH) * 0.5f + (1.0f - t) * 7.0f;
    y = (std::max)(rect.top, y);

    DrawSingleLine(m_lyric.previousText, m_res->subFormat, m_res->grayBrush,
        D2D1::RectF(rect.left, y, rect.right, y + rowH), 0.28f * lineAlpha, false);
    y += rowH + lineGap;

    const D2D1_RECT_F currentRect = D2D1::RectF(rect.left, y, rect.right, y + rowH);
    if (translationOnly || !m_lyric.hasExplicitWordTiming || m_lyric.words.empty()) {
        DrawScrollingLine(currentText, currentRect, lineAlpha, currentTimeMs);
    } else {
        DrawKaraokeLine(currentRect, lineAlpha);
    }
    y += rowH;

    if (ShouldShowTranslation(true)) {
        DrawSingleLine(m_lyric.translation, m_res->subFormat, m_res->whiteBrush,
            D2D1::RectF(rect.left, y, rect.right, y + transH), 0.62f * lineAlpha, false);
        y += transH + lineGap;
    } else {
        y += lineGap;
    }

    DrawSingleLine(m_lyric.nextText, m_res->subFormat, m_res->grayBrush,
        D2D1::RectF(rect.left, y, rect.right, y + rowH), 0.42f * lineAlpha, false);
}
