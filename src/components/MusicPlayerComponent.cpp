#include "components/MusicPlayerComponent.h"
#include "Constants.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>

namespace {
float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

float SmoothStep(float value) {
    value = Clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

D2D1_RECT_F LerpRect(const D2D1_RECT_F& a, const D2D1_RECT_F& b, float t) {
    return D2D1::RectF(
        Lerp(a.left, b.left, t),
        Lerp(a.top, b.top, t),
        Lerp(a.right, b.right, t),
        Lerp(a.bottom, b.bottom, t));
}

float MusicExpansionProgress(float height) {
    return SmoothStep((height - Constants::Size::MUSIC_COMPACT_HEIGHT) /
        ((std::max)(1.0f, Constants::Size::MUSIC_EXPANDED_HEIGHT - Constants::Size::MUSIC_COMPACT_HEIGHT)));
}

std::wstring FormatPlaybackTime(int64_t ms) {
    if (ms < 0) ms = 0;
    const int64_t totalSeconds = ms / 1000;
    const int64_t minutes = totalSeconds / 60;
    const int64_t seconds = totalSeconds % 60;
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%lld:%02lld", minutes, seconds);
    return buffer;
}
}

void MusicPlayerComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void MusicPlayerComponent::Update(float deltaTime) {
    if (m_isPlaying) {
        m_vinylAngle = std::fmod(m_vinylAngle + deltaTime * 54.0f, 360.0f);
        m_ringPhase = std::fmod(m_ringPhase + deltaTime * 2.8f, 6.2831853f);
    }

    const float targetLevel = (m_isPlaying && m_vinylRingPulse) ? (0.5f + 0.5f * std::sin(m_ringPhase)) : 0.0f;
    m_audioLevel += (targetLevel - m_audioLevel) * (m_isPlaying ? 0.14f : 0.08f);
    const bool hoverControls = m_hoveredButton != -1 || m_pressedButton != -1 || m_hoveredProgress != -1 || m_pressedProgress != -1;
    m_controlHoverAlpha += ((hoverControls ? 1.0f : 0.0f) - m_controlHoverAlpha) * 0.22f;

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
    const std::wstring& title, const std::wstring& artist, int64_t positionMs, int64_t durationMs) {
    m_hasSession = hasSession;
    m_isPlaying = isPlaying;
    m_progress = progress;
    m_title = title;
    m_artist = artist;
    m_positionMs = positionMs;
    m_durationMs = durationMs;
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
    float right = left + width;
    const float artSize = 64.0f;
    const float artLeft = left + 26.0f;
    const float artTop = top + 28.0f;

    if (m_expandedArtworkStyle == MusicArtworkStyle::Vinyl) {
        RenderVinylRecord(artLeft, artTop, artSize, contentAlpha);
    } else {
        RenderAlbumArt(artLeft, artTop, artSize, contentAlpha);
    }

    const float textLeft = artLeft + artSize + 24.0f;
    RenderTextLine(m_title, m_res->titleFormat, m_res->whiteBrush,
        D2D1::RectF(textLeft, top + 20.0f, right - 24.0f, top + 42.0f), contentAlpha, false);
    RenderTextLine(m_artist, m_res->subFormat, m_res->grayBrush,
        D2D1::RectF(textLeft, top + 43.0f, right - 24.0f, top + 61.0f), 0.72f * contentAlpha, false);

    const float controlAlpha = contentAlpha;
    float progressY = top + height - 66.0f;
    const float timeWidth = 44.0f;
    const float progressLeft = left + 66.0f;
    const float progressWidth = width - 132.0f;
    if (m_durationMs > 0) {
        RenderTextLine(FormatPlaybackTime(m_positionMs), m_res->subFormat, m_res->grayBrush,
            D2D1::RectF(left + 16.0f, progressY - 7.0f, left + 16.0f + timeWidth, progressY + 13.0f), 0.62f * controlAlpha, false);
        RenderTextLine(FormatPlaybackTime(m_durationMs), m_res->subFormat, m_res->grayBrush,
            D2D1::RectF(right - 16.0f - timeWidth, progressY - 7.0f, right - 16.0f, progressY + 13.0f), 0.62f * controlAlpha, true);
    }
    RenderProgressBar(progressLeft, progressY, progressWidth, 3.5f, controlAlpha);
    float buttonGroupWidth = 36.0f * 4.0f + 40.0f + 22.0f * 4.0f;
    float buttonX = left + (width - buttonGroupWidth) / 2.0f;
    RenderPlaybackButtons(buttonX, progressY + 18.0f, 36.0f, controlAlpha, true);
}

void MusicPlayerComponent::RenderCompact(float left, float top, float width, float height, float contentAlpha) {
    const MusicVisualLayout layout = BuildVisualLayout(left, top, width, height, MusicExpansionProgress(height));
    const float recordSize = layout.artRect.right - layout.artRect.left;
    const float controlsAlpha = contentAlpha * m_controlHoverAlpha;
    const float controlsLeft = width + left - 104.0f;
    const float compactTextRight = (controlsAlpha > 0.02f)
        ? (std::max)(layout.titleRect.left + 62.0f, controlsLeft - 8.0f)
        : layout.titleRect.right;

    if (m_compactArtworkStyle == MusicArtworkStyle::Vinyl) {
        RenderVinylRecord(layout.artRect.left, layout.artRect.top, recordSize, contentAlpha);
    } else {
        RenderAlbumArt(layout.artRect.left, layout.artRect.top, recordSize, contentAlpha);
    }
    RenderCompactText(m_title, layout.titleRect.left, layout.titleRect.top, compactTextRight,
        layout.titleRect.bottom - layout.titleRect.top, contentAlpha);
    D2D1_RECT_F artistRect = layout.artistRect;
    artistRect.right = compactTextRight;
    RenderTextLine(m_artist, m_res->subFormat, m_res->grayBrush, artistRect, 0.78f * contentAlpha, false);

    if (controlsAlpha > 0.02f) {
        RenderPlaybackButtons(controlsLeft, top + (height - 28.0f) * 0.5f, 28.0f, controlsAlpha);
    }
}

MusicPlayerComponent::MusicVisualLayout MusicPlayerComponent::BuildVisualLayout(
    float left,
    float top,
    float width,
    float height,
    float expansion) const {
    const float right = left + width;
    const float compactArtSize = 36.0f;
    const D2D1_RECT_F compactArt = D2D1::RectF(
        left + 10.0f,
        top + (height - compactArtSize) * 0.5f,
        left + 10.0f + compactArtSize,
        top + (height - compactArtSize) * 0.5f + compactArtSize);
    const float compactTextLeft = compactArt.right + 12.0f;
    const float compactTextRight = right - 58.0f;
    const D2D1_RECT_F compactTitle = D2D1::RectF(compactTextLeft, top + 5.0f, compactTextRight, top + 25.0f);
    const D2D1_RECT_F compactArtist = D2D1::RectF(compactTextLeft, top + 25.0f, compactTextRight, top + 43.0f);

    const float expandedArtSize = ALBUM_ART_SIZE;
    const D2D1_RECT_F expandedArt = D2D1::RectF(
        left + ALBUM_ART_MARGIN,
        top + 26.0f,
        left + ALBUM_ART_MARGIN + expandedArtSize,
        top + 26.0f + expandedArtSize);
    const float expandedTextLeft = expandedArt.right + 15.0f;
    const float expandedTextRight = right - 20.0f;
    const D2D1_RECT_F expandedTitle = D2D1::RectF(expandedTextLeft, top + 20.0f, expandedTextRight, top + 44.0f);
    const D2D1_RECT_F expandedArtist = D2D1::RectF(expandedTextLeft, top + 45.0f, expandedTextRight, top + 64.0f);

    MusicVisualLayout layout;
    layout.artRect = LerpRect(compactArt, expandedArt, expansion);
    layout.titleRect = LerpRect(compactTitle, expandedTitle, expansion);
    layout.artistRect = LerpRect(compactArtist, expandedArtist, expansion);
    return layout;
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

void MusicPlayerComponent::RenderVinylRecord(float left, float top, float size, float alpha) {
    auto* ctx = m_res->d2dContext;
    if (!ctx) return;

    const D2D1_POINT_2F center = D2D1::Point2F(left + size * 0.5f, top + size * 0.5f);
    const float radius = size * 0.5f;
    const float labelRadius = size * 0.40f;

    m_res->blackBrush->SetOpacity(0.30f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x + size * 0.08f, center.y + size * 0.10f), radius * 0.96f, radius * 0.50f), m_res->blackBrush);

    m_res->darkGrayBrush->SetOpacity(0.62f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(center, radius + 2.4f, radius + 2.4f), m_res->darkGrayBrush);
    m_res->blackBrush->SetOpacity(0.96f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(center, radius, radius), m_res->blackBrush);

    if (m_res->grayBrush) {
        for (int i = 0; i < 10; ++i) {
            const float grooveRadius = radius - 3.0f - static_cast<float>(i) * (radius * 0.062f);
            if (grooveRadius <= labelRadius + 2.0f) {
                break;
            }
            m_res->grayBrush->SetOpacity((0.16f - i * 0.010f) * alpha);
            ctx->DrawEllipse(D2D1::Ellipse(center, grooveRadius, grooveRadius), m_res->grayBrush, i % 3 == 0 ? 1.0f : 0.65f);
        }
        m_res->whiteBrush->SetOpacity(0.10f * alpha);
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x - radius * 0.24f, center.y - radius * 0.28f),
            radius * 0.18f, radius * 0.08f), m_res->whiteBrush);
        if (m_res->d2dFactory) {
            ComPtr<ID2D1PathGeometry> arcGeometry;
            if (SUCCEEDED(m_res->d2dFactory->CreatePathGeometry(&arcGeometry)) && arcGeometry) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(arcGeometry->Open(&sink)) && sink) {
                    sink->BeginFigure(
                        D2D1::Point2F(center.x - radius * 0.22f, center.y - radius * 0.70f),
                        D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddArc(D2D1::ArcSegment(
                        D2D1::Point2F(center.x + radius * 0.42f, center.y - radius * 0.52f),
                        D2D1::SizeF(radius * 0.78f, radius * 0.78f),
                        0.0f,
                        D2D1_SWEEP_DIRECTION_CLOCKWISE,
                        D2D1_ARC_SIZE_SMALL));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    if (SUCCEEDED(sink->Close())) {
                        m_res->whiteBrush->SetOpacity(0.24f * alpha);
                        ctx->DrawGeometry(arcGeometry.Get(), m_res->whiteBrush, 1.4f);
                    }
                }
            }
        }
    }

    D2D1_ELLIPSE labelEllipse = D2D1::Ellipse(center, labelRadius, labelRadius);
    if (m_albumBitmap && m_res->d2dFactory) {
        ComPtr<ID2D1EllipseGeometry> clipGeometry;
        if (SUCCEEDED(m_res->d2dFactory->CreateEllipseGeometry(&labelEllipse, &clipGeometry)) && clipGeometry) {
            ComPtr<ID2D1Layer> layer;
            ctx->CreateLayer(&layer);
            if (layer) {
                const D2D1_RECT_F labelRect = D2D1::RectF(
                    center.x - labelRadius,
                    center.y - labelRadius,
                    center.x + labelRadius,
                    center.y + labelRadius);
                D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters(labelRect, clipGeometry.Get());
                ctx->PushLayer(&layerParams, layer.Get());
                D2D1_MATRIX_3X2_F oldTransform;
                ctx->GetTransform(&oldTransform);
                ctx->SetTransform(D2D1::Matrix3x2F::Rotation(m_vinylAngle, center) * oldTransform);
                ctx->DrawBitmap(m_albumBitmap.Get(), labelRect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                ctx->SetTransform(oldTransform);
                ctx->PopLayer();
            }
        }
    } else {
        m_res->themeBrush->SetOpacity(0.90f * alpha);
        ctx->FillEllipse(labelEllipse, m_res->themeBrush);
    }

    m_res->blackBrush->SetOpacity(0.76f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(center, 3.6f, 3.6f), m_res->blackBrush);
    m_res->whiteBrush->SetOpacity(0.58f * alpha);
    ctx->DrawEllipse(D2D1::Ellipse(center, 5.2f, 5.2f), m_res->whiteBrush, 0.75f);
    m_res->whiteBrush->SetOpacity(0.42f * alpha);
    ctx->DrawEllipse(D2D1::Ellipse(center, labelRadius, labelRadius), m_res->whiteBrush, 0.8f);
    m_res->whiteBrush->SetOpacity(0.22f * alpha);
    ctx->DrawEllipse(D2D1::Ellipse(center, radius - 1.0f, radius - 1.0f), m_res->whiteBrush, 0.9f);
}

void MusicPlayerComponent::RenderVinylRings(float left, float top, float size, float alpha) {
    if (!m_res || !m_res->d2dContext || !m_res->whiteBrush || alpha <= 0.01f) {
        return;
    }

    const D2D1_POINT_2F center = D2D1::Point2F(left + size * 0.5f, top + size * 0.5f);
    const float baseRadius = size * 0.5f;
    const float pulse = m_vinylRingPulse ? m_audioLevel : 0.0f;
    const int ringCount = m_vinylRingPulse ? 3 : 1;
    for (int i = 0; i < ringCount; ++i) {
        const float t = ringCount == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(ringCount - 1);
        const float radius = baseRadius + 7.0f + i * 7.0f + pulse * (14.0f + i * 2.0f);
        const float opacity = (m_isPlaying ? 0.24f : 0.12f) * (1.0f - t * 0.72f) * alpha;
        m_res->whiteBrush->SetOpacity(opacity);
        m_res->d2dContext->DrawEllipse(D2D1::Ellipse(center, radius, radius), m_res->whiteBrush, 1.5f);
    }
}

void MusicPlayerComponent::RenderProgressBar(float left, float top, float width, float height, float alpha) {
    if (width <= 0) return;
    auto* ctx = m_res->d2dContext;
    float radius = height / 2.0f;
    float actualHeight = height;
    float actualTop = top;
    if (m_hoveredProgress != -1 || m_pressedProgress != -1) {
        actualHeight = 6.0f;
        actualTop = top - 1.25f;
    }
    radius = actualHeight / 2.0f;

    if (m_res->progressBgBrush) {
        m_res->progressBgBrush->SetOpacity(0.5f * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, actualTop, left + width, actualTop + actualHeight), radius, radius), m_res->progressBgBrush);
    }
    if (m_res->progressFgBrush && m_progress > 0.0f) {
        m_res->progressFgBrush->SetOpacity(alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, actualTop, left + width * m_progress, actualTop + actualHeight), radius, radius), m_res->progressFgBrush);
    }
}

void MusicPlayerComponent::RenderPlaybackButtons(float left, float top, float buttonSize, float alpha, bool expanded) {
    auto* ctx = m_res->d2dContext;
    const int count = expanded ? 5 : 3;

    for (int i = 0; i < count; ++i) {
        const bool playButton = expanded && i == 2;
        const float currentSize = playButton ? 40.0f : buttonSize;
        float x = left + i * (buttonSize + (expanded ? 22.0f : BUTTON_SPACING));
        if (expanded && i > 2) x += 4.0f;
        D2D1_RECT_F r = D2D1::RectF(x, top + (buttonSize - currentSize) * 0.5f, x + currentSize, top + (buttonSize - currentSize) * 0.5f + currentSize);
        bool hovered = (i == m_hoveredButton);
        bool pressed = (i == m_pressedButton);

        if (playButton) {
            m_res->whiteBrush->SetOpacity(alpha);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                currentSize * 0.5f, currentSize * 0.5f), m_res->whiteBrush);
            RenderPlaybackIcon(m_isPlaying ? 2 : 5, D2D1::RectF(r.left + 10.0f, r.top + 10.0f, r.right - 10.0f, r.bottom - 10.0f),
                m_res->blackBrush, alpha, true);
            continue;
        }

        const bool compactButton = !expanded;
        if (alpha > 0.1f && m_res->buttonHoverBrush && (compactButton || hovered || pressed)) {
            float bgOp = compactButton ? 0.12f : 0.0f;
            if (hovered) bgOp = compactButton ? 0.22f : 0.08f;
            if (pressed) bgOp = compactButton ? 0.28f : 0.14f;
            m_res->buttonHoverBrush->SetOpacity(bgOp * alpha);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                currentSize * 0.5f, currentSize * 0.5f), m_res->buttonHoverBrush);
        }

        const int iconKind = expanded
            ? i
            : (i == 0 ? 1 : (i == 1 ? (m_isPlaying ? 2 : 5) : 3));
        ID2D1Brush* iconBrush = (expanded && i == 0 && m_liked && m_res->themeBrush)
            ? m_res->themeBrush
            : m_res->whiteBrush;
        const float iconAlpha = (expanded ? 0.86f : 1.0f) * alpha;
        const float inset = compactButton ? 7.0f : 8.0f;
        const D2D1_RECT_F iconRect = D2D1::RectF(r.left + inset, r.top + inset, r.right - inset, r.bottom - inset);

        if (pressed) {
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            const float cx = (r.left + r.right) * 0.5f;
            const float cy = (r.top + r.bottom) * 0.5f;
            ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.96f, 0.96f, D2D1::Point2F(cx, cy)) * old);
            RenderPlaybackIcon(iconKind, iconRect, iconBrush, iconAlpha, expanded && i == 0 && m_liked);
            ctx->SetTransform(old);
        } else {
            RenderPlaybackIcon(iconKind, iconRect, iconBrush, iconAlpha, expanded && i == 0 && m_liked);
        }
    }
}

void MusicPlayerComponent::RenderPlaybackIcon(int iconKind, const D2D1_RECT_F& rect, ID2D1Brush* brush, float alpha, bool filled) {
    if (!m_res || !m_res->d2dContext || !m_res->d2dFactory || !brush || alpha <= 0.01f) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    brush->SetOpacity(alpha);
    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;

    auto fillTriangle = [&](D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c) {
        ComPtr<ID2D1PathGeometry> geometry;
        if (FAILED(m_res->d2dFactory->CreatePathGeometry(&geometry)) || !geometry) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geometry->Open(&sink)) || !sink) return;
        sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(b);
        sink->AddLine(c);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (SUCCEEDED(sink->Close())) {
            ctx->FillGeometry(geometry.Get(), brush);
        }
    };

    switch (iconKind) {
    case 0: { // like
        ComPtr<ID2D1PathGeometry> geometry;
        if (FAILED(m_res->d2dFactory->CreatePathGeometry(&geometry)) || !geometry) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geometry->Open(&sink)) || !sink) return;
        sink->BeginFigure(D2D1::Point2F(cx, rect.bottom - h * 0.10f),
            filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(rect.left + w * 0.02f, rect.top + h * 0.55f),
            D2D1::Point2F(rect.left + w * 0.10f, rect.top + h * 0.02f),
            D2D1::Point2F(cx, rect.top + h * 0.30f)));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(rect.right - w * 0.10f, rect.top + h * 0.02f),
            D2D1::Point2F(rect.right - w * 0.02f, rect.top + h * 0.55f),
            D2D1::Point2F(cx, rect.bottom - h * 0.10f)));
        sink->EndFigure(filled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        if (SUCCEEDED(sink->Close())) {
            if (filled) ctx->FillGeometry(geometry.Get(), brush);
            else ctx->DrawGeometry(geometry.Get(), brush, 1.8f);
        }
        break;
    }
    case 1: // previous
        ctx->DrawLine(D2D1::Point2F(rect.left + w * 0.22f, rect.top + h * 0.18f),
            D2D1::Point2F(rect.left + w * 0.22f, rect.bottom - h * 0.18f), brush, 1.9f);
        fillTriangle(D2D1::Point2F(rect.right - w * 0.16f, rect.top + h * 0.16f),
            D2D1::Point2F(rect.right - w * 0.16f, rect.bottom - h * 0.16f),
            D2D1::Point2F(rect.left + w * 0.34f, cy));
        break;
    case 2: { // pause
        const float barW = w * 0.20f;
        const float gap = w * 0.14f;
        const float top = rect.top + h * 0.12f;
        const float bottom = rect.bottom - h * 0.12f;
        ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - gap * 0.5f - barW, top, cx - gap * 0.5f, bottom), barW * 0.45f, barW * 0.45f), brush);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + gap * 0.5f, top, cx + gap * 0.5f + barW, bottom), barW * 0.45f, barW * 0.45f), brush);
        break;
    }
    case 3: // next
        ctx->DrawLine(D2D1::Point2F(rect.right - w * 0.22f, rect.top + h * 0.18f),
            D2D1::Point2F(rect.right - w * 0.22f, rect.bottom - h * 0.18f), brush, 1.9f);
        fillTriangle(D2D1::Point2F(rect.left + w * 0.16f, rect.top + h * 0.16f),
            D2D1::Point2F(rect.left + w * 0.16f, rect.bottom - h * 0.16f),
            D2D1::Point2F(rect.right - w * 0.34f, cy));
        break;
    case 4: { // music note / lyrics mode
        const float lineLeft = rect.left + w * 0.06f;
        const float lineRight = rect.left + w * 0.60f;
        const float lineH = h * 0.10f;
        const float ys[3] = { rect.top + h * 0.22f, rect.top + h * 0.48f, rect.top + h * 0.74f };
        for (int row = 0; row < 3; ++row) {
            const float right = row == 2 ? rect.left + w * 0.44f : lineRight;
            ctx->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(lineLeft, ys[row] - lineH * 0.5f, right, ys[row] + lineH * 0.5f),
                lineH * 0.5f, lineH * 0.5f), brush);
        }
        fillTriangle(D2D1::Point2F(rect.left + w * 0.70f, rect.top + h * 0.22f),
            D2D1::Point2F(rect.left + w * 0.70f, rect.bottom - h * 0.22f),
            D2D1::Point2F(rect.right - w * 0.08f, cy));
        break;
    }
    case 5: // play
    default:
        fillTriangle(D2D1::Point2F(rect.left + w * 0.28f, rect.top + h * 0.14f),
            D2D1::Point2F(rect.left + w * 0.28f, rect.bottom - h * 0.14f),
            D2D1::Point2F(rect.right - w * 0.12f, cy));
        break;
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
        ctx->DrawTextLayout(D2D1::Point2F(left, yPos), layout.Get(), m_res->whiteBrush);
    }

    ctx->PopAxisAlignedClip();
}

void MusicPlayerComponent::RenderTextLine(
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const D2D1_RECT_F& rect,
    float alpha,
    bool center) {
    if (text.empty() || !format || !brush || rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(
        text.c_str(), (UINT32)text.size(),
        format, rect.right - rect.left, rect.bottom - rect.top, &layout);
    if (!layout) {
        return;
    }

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim{};
    trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    ComPtr<IDWriteInlineObject> ellipsis;
    m_res->dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis);
    layout->SetTrimming(&trim, ellipsis.Get());
    layout->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    brush->SetOpacity(alpha);
    ctx->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ctx->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush);
    ctx->PopAxisAlignedClip();
}
