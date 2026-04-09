#include "components/MusicPlayerComponent.h"
#include <wincodec.h>
#include <cmath>

void MusicPlayerComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void MusicPlayerComponent::SetPlaybackState(bool hasSession, bool isPlaying, float progress,
    const std::wstring& title, const std::wstring& artist,
    const LyricData& lyric, bool showTime, const std::wstring& timeText) {
    m_hasSession = hasSession;
    m_isPlaying  = isPlaying;
    m_progress   = progress;
    m_title      = title;
    m_artist     = artist;
    m_lyric      = lyric;
    m_showTime   = showTime;
    m_timeText   = timeText;
}

void MusicPlayerComponent::SetInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress) {
    m_hoveredButton   = hoveredButton;
    m_pressedButton   = pressedButton;
    m_hoveredProgress = hoveredProgress;
    m_pressedProgress = pressedProgress;
}

void MusicPlayerComponent::SetScrollState(float titleScrollOffset, float lyricScrollOffset,
    bool titleScrolling, bool lyricScrolling) {
    m_titleScrollOffset = titleScrollOffset;
    m_lyricScrollOffset = lyricScrollOffset;
    m_titleScrolling    = titleScrolling;
    m_lyricScrolling    = lyricScrolling;
}

bool MusicPlayerComponent::LoadAlbumArtFromMemory(const std::vector<uint8_t>& data) {
    if (!m_res || !m_res->wicFactory || !m_res->d2dContext || data.empty()) return false;

    auto* wic = m_res->wicFactory;
    auto* ctx = m_res->d2dContext;

    ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    stream->InitializeFromMemory(const_cast<uint8_t*>(data.data()), (DWORD)data.size());

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    ComPtr<IWICFormatConverter> converter;
    wic->CreateFormatConverter(&converter);
    converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    m_albumBitmap.Reset();
    return SUCCEEDED(ctx->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_albumBitmap));
}

void MusicPlayerComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res) return;
    float left = rect.left, top = rect.top;
    float width = rect.right - rect.left, height = rect.bottom - rect.top;
    bool compactMode = (height >= 35.0f && height < COMPACT_THRESHOLD);

    if (compactMode)
        RenderCompact(left, top, width, height, contentAlpha);
    else
        RenderExpanded(left, top, width, height, contentAlpha);
}

void MusicPlayerComponent::RenderExpanded(float left, float top, float width, float height, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float right = left + width;
    float artLeft = left + ALBUM_ART_MARGIN;
    float artTop  = top + 30.0f;
    float textLeft = artLeft + ALBUM_ART_SIZE + 15.0f;
    float textRight = right - 20.0f;
    float titleMaxWidth = textRight - textLeft;

    if (!m_hasSession) return;

    RenderAlbumArt(artLeft, artTop, ALBUM_ART_SIZE, contentAlpha);

    // Waveform (播放时显示在文本区右侧)
    if (m_isPlaying) {
        float waveRight  = right - 40.0f;
        float baseBottom = artTop + 45.0f;
        float h1 = m_waveH[0] * 0.5f, h2 = m_waveH[1] * 0.5f, h3 = m_waveH[2] * 0.5f;
        m_res->themeBrush->SetOpacity(contentAlpha);
        ctx->FillRectangle(D2D1::RectF(waveRight - 20.0f, baseBottom - h1, waveRight - 16.0f, baseBottom), m_res->themeBrush);
        ctx->FillRectangle(D2D1::RectF(waveRight - 12.0f, baseBottom - h2, waveRight -  8.0f, baseBottom), m_res->themeBrush);
        ctx->FillRectangle(D2D1::RectF(waveRight -  4.0f, baseBottom - h3, waveRight,         baseBottom), m_res->themeBrush);
    }

    // Lyrics
    if (!m_lyric.text.empty())
        RenderLyrics(textLeft, artTop, textRight - textLeft, contentAlpha);

    // "Title - Artist" subtitle
    std::wstring combined = m_title;
    if (!m_artist.empty()) combined += L" - " + m_artist;
    if (!combined.empty() && m_res->subFormat) {
        ComPtr<IDWriteTextLayout> layout;
        m_res->dwriteFactory->CreateTextLayout(combined.c_str(), (UINT32)combined.size(),
            m_res->subFormat, titleMaxWidth - 60.0f, 100.0f, &layout);
        if (layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{}; trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            ComPtr<IDWriteInlineObject> ellipsis;
            m_res->dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis);
            layout->SetTrimming(&trim, ellipsis.Get());
            m_res->grayBrush->SetOpacity(contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 25.0f), layout.Get(), m_res->grayBrush);
        }
    }

    // Progress bar
    float artistBottom = artTop + 60.0f;
    float progressY = artistBottom + 20.0f;
    RenderProgressBar(textLeft - 80.0f, progressY, textLeft + titleMaxWidth - (textLeft - 80.0f), 6.0f, contentAlpha);

    // Playback buttons
    float buttonGroupWidth = BUTTON_SIZE * 3 + BUTTON_SPACING * 2;
    float buttonX = textLeft + (titleMaxWidth - buttonGroupWidth) / 2.0f - 45.0f;
    if (buttonX < textLeft) buttonX = textLeft;
    RenderPlaybackButtons(buttonX, progressY + 6.0f + 10.0f, BUTTON_SIZE, contentAlpha);
}

void MusicPlayerComponent::RenderCompact(float left, float top, float width, float height, float contentAlpha) {
    // Time display (paused)
    if (m_showTime && !m_timeText.empty() && !m_isPlaying && height >= 35.0f) {
        RenderCompactTime(m_timeText, left, top, width, height, contentAlpha);
        return;
    }
    // Scrolling title (playing)
    if (m_isPlaying) {
        float waveRight = left + width - 20.0f;
        float textLeft = left + 15.0f;
        float textRight = waveRight - 30.0f;
        RenderCompactText(m_title, textLeft, top, textRight, height, contentAlpha);
    }
}

void MusicPlayerComponent::RenderAlbumArt(float left, float top, float size, float alpha) {
    auto* ctx = m_res->d2dContext;
    D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(left, top, left + size, top + size), 12.0f, 12.0f);
    if (m_albumBitmap) {
        ctx->DrawBitmap(m_albumBitmap.Get(), artRect.rect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        m_res->themeBrush->SetOpacity(alpha);
        ctx->FillRoundedRectangle(&artRect, m_res->themeBrush);
    }
}

void MusicPlayerComponent::RenderProgressBar(float left, float top, float width, float height, float alpha) {
    if (width <= 0) return;
    auto* ctx = m_res->d2dContext;
    float radius = height / 2.0f;
    float actualHeight = height, actualTop = top;
    if (m_hoveredProgress != -1 || m_pressedProgress != -1) { actualHeight = 10.0f; actualTop = top - 2.0f; }

    if (m_res->progressBgBrush) {
        m_res->progressBgBrush->SetOpacity(0.5f * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, actualTop, left + width, actualTop + actualHeight), radius, radius), m_res->progressBgBrush);
    }
    if (m_res->progressFgBrush && m_progress > 0.0f) {
        m_res->progressFgBrush->SetOpacity(alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, actualTop, left + width * m_progress, actualTop + actualHeight), radius, radius), m_res->progressFgBrush);
    }
}

void MusicPlayerComponent::RenderPlaybackButtons(float left, float top, float buttonSize, float alpha) {
    auto* ctx = m_res->d2dContext;
    m_res->whiteBrush->SetOpacity(alpha);

    const wchar_t icons[3] = { L'\uE892',
        m_isPlaying ? L'\uE769' : L'\uE768',
        L'\uE893' };

    for (int i = 0; i < 3; i++) {
        float x = left + i * (buttonSize + BUTTON_SPACING);
        D2D1_RECT_F r = D2D1::RectF(x, top, x + buttonSize, top + buttonSize);
        bool hovered = (i == m_hoveredButton);
        bool pressed = (i == m_pressedButton);

        // Hover / press background
        if (alpha > 0.1f && (hovered || pressed) && m_res->buttonHoverBrush) {
            float bgOp = pressed ? 0.25f : 0.12f;
            m_res->buttonHoverBrush->SetOpacity(bgOp * alpha);
            ctx->FillRoundedRectangle(D2D1::RoundedRect(r, buttonSize / 2.0f, buttonSize / 2.0f), m_res->buttonHoverBrush);
        }

        // Icon (scale down when pressed)
        if (pressed) {
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            float cx = x + buttonSize / 2.0f, cy = top + buttonSize / 2.0f;
            ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.8f, 0.8f, D2D1::Point2F(cx, cy)) * old);
            ctx->DrawText(&icons[i], 1, m_res->iconFormat, r, m_res->whiteBrush);
            ctx->SetTransform(old);
        } else {
            ctx->DrawText(&icons[i], 1, m_res->iconFormat, r, m_res->whiteBrush);
        }
    }
}

void MusicPlayerComponent::RenderLyrics(float left, float top, float width, float alpha) {
    if (m_lyric.text.empty()) return;
    auto* ctx = m_res->d2dContext;
    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(m_lyric.text.c_str(), (UINT32)m_lyric.text.size(),
        m_res->titleFormat, width, 100.0f, &layout);
    if (!layout) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    bool needScroll = (metrics.width > width);

    D2D1_RECT_F clip = D2D1::RectF(left, top, left + width, top + 25.0f);
    ctx->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_res->whiteBrush->SetOpacity(alpha);
    if (needScroll) {
        float offset = fmodf(m_lyricScrollOffset, metrics.width);
        ctx->DrawTextLayout(D2D1::Point2F(left - offset, top), layout.Get(), m_res->whiteBrush);
        ctx->DrawTextLayout(D2D1::Point2F(left - offset + metrics.width, top), layout.Get(), m_res->whiteBrush);
    } else {
        ctx->DrawTextLayout(D2D1::Point2F(left, top), layout.Get(), m_res->whiteBrush);
    }
    ctx->PopAxisAlignedClip();
}

void MusicPlayerComponent::RenderCompactText(const std::wstring& text, float left, float top,
    float textRight, float height, float alpha) {
    if (text.empty()) return;
    auto* ctx = m_res->d2dContext;
    float maxWidth = textRight - left;
    if (maxWidth <= 0) return;
    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
        m_res->titleFormat, maxWidth, 100.0f, &layout);
    if (!layout) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    bool needScroll = (metrics.width > maxWidth);
    float yPos = top + (height - metrics.height) / 2.0f;
    m_res->whiteBrush->SetOpacity(alpha);
    ctx->PushAxisAlignedClip(D2D1::RectF(left, top, textRight, top + height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (needScroll) {
        float offset = fmodf(m_titleScrollOffset, metrics.width);
        ctx->DrawTextLayout(D2D1::Point2F(left - offset, yPos), layout.Get(), m_res->whiteBrush);
        ctx->DrawTextLayout(D2D1::Point2F(left - offset + metrics.width, yPos), layout.Get(), m_res->whiteBrush);
    } else {
        float textOffset = (maxWidth - metrics.width) / 2.0f;
        ctx->DrawTextLayout(D2D1::Point2F(left + textOffset, yPos), layout.Get(), m_res->whiteBrush);
    }
    ctx->PopAxisAlignedClip();
}

void MusicPlayerComponent::RenderCompactTime(const std::wstring& timeText, float left, float top,
    float width, float height, float alpha) {
    if (timeText.empty()) return;
    auto* ctx = m_res->d2dContext;
    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(timeText.c_str(), (UINT32)timeText.size(),
        m_res->titleFormat, 200.0f, 100.0f, &layout);
    if (!layout) return;
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    m_res->whiteBrush->SetOpacity(alpha);
    ctx->DrawTextLayout(D2D1::Point2F(left + (width - metrics.width) / 2.0f, top + (height - metrics.height) / 2.0f),
        layout.Get(), m_res->whiteBrush);
}
