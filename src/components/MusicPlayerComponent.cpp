#include "components/MusicPlayerComponent.h"
#include "Constants.h"
#include <wincodec.h>
#include <cmath>

void MusicPlayerComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void MusicPlayerComponent::Update(float deltaTime) {
    if (!m_isCompact || !m_titleScrolling) {
        if (!m_isCompact) {
            m_titleScrollOffset = 0.0f;
            m_titleScrolling = false;
        }
        return;
    }

    m_titleScrollOffset += Constants::Animation::SCROLL_SPEED * deltaTime;
}

void MusicPlayerComponent::SetPlaybackState(bool hasSession, bool isPlaying, float progress,
    const std::wstring& title, const std::wstring& artist) {
    m_hasSession = hasSession;
    m_isPlaying = isPlaying;
    m_progress = progress;
    m_title = title;
    m_artist = artist;
}

void MusicPlayerComponent::SetInteractionState(int hoveredButton, int pressedButton, int hoveredProgress, int pressedProgress) {
    m_hoveredButton = hoveredButton;
    m_pressedButton = pressedButton;
    m_hoveredProgress = hoveredProgress;
    m_pressedProgress = pressedProgress;
}

bool MusicPlayerComponent::LoadAlbumArt(const std::wstring& file) {
    if (!m_res || !m_res->wicFactory || !m_res->d2dContext) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(m_res->wicFactory->CreateDecoderFromFilename(
        file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(m_res->wicFactory->CreateFormatConverter(&converter))) return false;

    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        return false;
    }

    m_albumBitmap.Reset();
    return SUCCEEDED(m_res->d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_albumBitmap));
}

bool MusicPlayerComponent::LoadAlbumArtFromMemory(const std::vector<uint8_t>& data) {
    if (!m_res || !m_res->wicFactory || !m_res->d2dContext || data.empty()) return false;

    ComPtr<IWICStream> stream;
    m_res->wicFactory->CreateStream(&stream);
    stream->InitializeFromMemory(const_cast<uint8_t*>(data.data()), (DWORD)data.size());

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(m_res->wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    ComPtr<IWICFormatConverter> converter;
    m_res->wicFactory->CreateFormatConverter(&converter);
    converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    m_albumBitmap.Reset();
    return SUCCEEDED(m_res->d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_albumBitmap));
}

void MusicPlayerComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_hasSession) return;

    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;
    m_isCompact = (height >= 35.0f && height < COMPACT_THRESHOLD);

    if (m_isCompact) {
        RenderCompact(left, top, width, height, contentAlpha);
    } else {
        RenderExpanded(left, top, width, height, contentAlpha);
    }
}

void MusicPlayerComponent::RenderExpanded(float left, float top, float width, float height, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float right = left + width;
    float artLeft = left + ALBUM_ART_MARGIN;
    float artTop = top + 30.0f;
    float textLeft = artLeft + ALBUM_ART_SIZE + 15.0f;
    float textRight = right - 20.0f;
    float titleMaxWidth = textRight - textLeft;

    RenderAlbumArt(artLeft, artTop, ALBUM_ART_SIZE, contentAlpha);

    std::wstring combined = m_title;
    if (!m_artist.empty()) combined += L" - " + m_artist;
    if (!combined.empty() && m_res->subFormat) {
        ComPtr<IDWriteTextLayout> layout;
        m_res->dwriteFactory->CreateTextLayout(combined.c_str(), (UINT32)combined.size(),
            m_res->subFormat, titleMaxWidth - 60.0f, 100.0f, &layout);
        if (layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{};
            trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            ComPtr<IDWriteInlineObject> ellipsis;
            m_res->dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis);
            layout->SetTrimming(&trim, ellipsis.Get());
            m_res->grayBrush->SetOpacity(contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 25.0f), layout.Get(), m_res->grayBrush);
        }
    }

    float artistBottom = artTop + 60.0f;
    float progressY = artistBottom + 20.0f;
    RenderProgressBar(textLeft - 80.0f, progressY, textLeft + titleMaxWidth - (textLeft - 80.0f), 6.0f, contentAlpha);

    float buttonGroupWidth = BUTTON_SIZE * 3 + BUTTON_SPACING * 2;
    float buttonX = textLeft + (titleMaxWidth - buttonGroupWidth) / 2.0f - 45.0f;
    if (buttonX < textLeft) buttonX = textLeft;
    RenderPlaybackButtons(buttonX, progressY + 16.0f, BUTTON_SIZE, contentAlpha);
}

void MusicPlayerComponent::RenderCompact(float left, float top, float width, float height, float contentAlpha) {
    float waveRight = left + width - 20.0f;
    float textLeft = left + 15.0f;
    float textRight = waveRight - 30.0f;
    RenderCompactText(m_title, textLeft, top, textRight, height, contentAlpha);
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
    float actualHeight = height;
    float actualTop = top;
    if (m_hoveredProgress != -1 || m_pressedProgress != -1) {
        actualHeight = 10.0f;
        actualTop = top - 2.0f;
    }

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

    const wchar_t icons[3] = { L'\uE892', m_isPlaying ? L'\uE769' : L'\uE768', L'\uE893' };

    for (int i = 0; i < 3; ++i) {
        float x = left + i * (buttonSize + BUTTON_SPACING);
        D2D1_RECT_F r = D2D1::RectF(x, top, x + buttonSize, top + buttonSize);
        bool hovered = (i == m_hoveredButton);
        bool pressed = (i == m_pressedButton);

        if (alpha > 0.1f && (hovered || pressed) && m_res->buttonHoverBrush) {
            float bgOp = pressed ? 0.25f : 0.12f;
            m_res->buttonHoverBrush->SetOpacity(bgOp * alpha);
            ctx->FillRoundedRectangle(D2D1::RoundedRect(r, buttonSize / 2.0f, buttonSize / 2.0f), m_res->buttonHoverBrush);
        }

        if (pressed) {
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            float cx = x + buttonSize / 2.0f;
            float cy = top + buttonSize / 2.0f;
            ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.8f, 0.8f, D2D1::Point2F(cx, cy)) * old);
            ctx->DrawText(&icons[i], 1, m_res->iconFormat, r, m_res->whiteBrush);
            ctx->SetTransform(old);
        } else {
            ctx->DrawText(&icons[i], 1, m_res->iconFormat, r, m_res->whiteBrush);
        }
    }
}

void MusicPlayerComponent::RenderCompactText(const std::wstring& text, float left, float top,
    float textRight, float height, float alpha) {
    if (text.empty()) return;

    auto* ctx = m_res->d2dContext;
    float maxWidth = textRight - left;
    if (maxWidth <= 0) return;

    if (text != m_lastDrawnFullText) {
        m_lastDrawnFullText = text;
        m_titleScrollOffset = 0.0f;
    }

    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
        m_res->titleFormat, maxWidth, 100.0f, &layout);
    if (!layout) return;

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    m_titleScrolling = (metrics.width > maxWidth);

    float yPos = top + (height - metrics.height) / 2.0f;
    m_res->whiteBrush->SetOpacity(alpha);
    ctx->PushAxisAlignedClip(D2D1::RectF(left, top, textRight, top + height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_titleScrolling) {
        float cycleWidth = metrics.width + 30.0f;
        float offset = fmodf(m_titleScrollOffset, cycleWidth);
        ctx->DrawTextLayout(D2D1::Point2F(left - offset, yPos), layout.Get(), m_res->whiteBrush);
        ctx->DrawTextLayout(D2D1::Point2F(left - offset + cycleWidth, yPos), layout.Get(), m_res->whiteBrush);
    } else {
        float textOffset = (maxWidth - metrics.width) / 2.0f;
        ctx->DrawTextLayout(D2D1::Point2F(left + textOffset, yPos), layout.Get(), m_res->whiteBrush);
    }

    ctx->PopAxisAlignedClip();
}
