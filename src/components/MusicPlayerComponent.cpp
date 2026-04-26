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
}

void MusicPlayerComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void MusicPlayerComponent::Update(float deltaTime) {
    if (m_isPlaying) {
        m_vinylAngle = std::fmod(m_vinylAngle + deltaTime * 54.0f, 360.0f);
    }

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
    float right = left + width;
    const float expansion = MusicExpansionProgress(height);
    const MusicVisualLayout layout = BuildVisualLayout(left, top, width, height, expansion);
    const float artLeft = layout.artRect.left;
    const float artTop = layout.artRect.top;
    const float artSize = layout.artRect.right - layout.artRect.left;

    if (m_expandedArtworkStyle == MusicArtworkStyle::Vinyl) {
        RenderVinylRecord(artLeft, artTop, artSize, contentAlpha);
        RenderVinylTonearm(artLeft, artTop, artSize, contentAlpha);
    } else {
        RenderAlbumArt(artLeft, artTop, artSize, contentAlpha);
    }

    RenderTextLine(m_title, m_res->titleFormat, m_res->whiteBrush, layout.titleRect, contentAlpha, false);
    RenderTextLine(m_artist, m_res->subFormat, m_res->grayBrush, layout.artistRect, 0.78f * contentAlpha, false);

    const float controlAlpha = contentAlpha * SmoothStep((expansion - 0.32f) / 0.68f);
    float progressY = top + (std::max)(92.0f, height - 56.0f);
    RenderProgressBar(left + 20.0f, progressY, right - left - 40.0f, 6.0f, controlAlpha);
    float buttonGroupWidth = BUTTON_SIZE * 3 + BUTTON_SPACING * 2;
    float buttonX = left + (width - buttonGroupWidth) / 2.0f;
    RenderPlaybackButtons(buttonX, progressY + 16.0f, BUTTON_SIZE, controlAlpha);
}

void MusicPlayerComponent::RenderCompact(float left, float top, float width, float height, float contentAlpha) {
    const MusicVisualLayout layout = BuildVisualLayout(left, top, width, height, MusicExpansionProgress(height));
    const float recordSize = layout.artRect.right - layout.artRect.left;

    if (m_compactArtworkStyle == MusicArtworkStyle::Vinyl) {
        RenderVinylRecord(layout.artRect.left, layout.artRect.top, recordSize, contentAlpha);
        RenderVinylTonearm(layout.artRect.left, layout.artRect.top, recordSize, contentAlpha);
    } else {
        RenderAlbumArt(layout.artRect.left, layout.artRect.top, recordSize, contentAlpha);
    }
    RenderCompactText(m_title, layout.titleRect.left, layout.titleRect.top, layout.titleRect.right,
        layout.titleRect.bottom - layout.titleRect.top, contentAlpha);

    RenderTextLine(m_artist, m_res->subFormat, m_res->grayBrush, layout.artistRect, 0.78f * contentAlpha, false);
}

MusicPlayerComponent::MusicVisualLayout MusicPlayerComponent::BuildVisualLayout(
    float left,
    float top,
    float width,
    float height,
    float expansion) const {
    const float right = left + width;
    const float compactArtSize = 44.0f;
    const D2D1_RECT_F compactArt = D2D1::RectF(
        left + 10.0f,
        top + (height - compactArtSize) * 0.5f,
        left + 10.0f + compactArtSize,
        top + (height - compactArtSize) * 0.5f + compactArtSize);
    const float compactTextLeft = compactArt.right + 10.0f;
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
    const float labelRadius = size * 0.28f;

    m_res->blackBrush->SetOpacity(0.30f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x + size * 0.08f, center.y + size * 0.10f), radius * 0.96f, radius * 0.50f), m_res->blackBrush);

    m_res->darkGrayBrush->SetOpacity(0.52f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(center, radius + 2.0f, radius + 2.0f), m_res->darkGrayBrush);
    m_res->blackBrush->SetOpacity(0.96f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(center, radius, radius), m_res->blackBrush);

    if (m_res->grayBrush) {
        for (int i = 0; i < 7; ++i) {
            const float grooveRadius = radius - 3.0f - static_cast<float>(i) * (radius * 0.085f);
            if (grooveRadius <= labelRadius + 3.0f) {
                break;
            }
            m_res->grayBrush->SetOpacity((0.13f - i * 0.010f) * alpha);
            ctx->DrawEllipse(D2D1::Ellipse(center, grooveRadius, grooveRadius), m_res->grayBrush, 0.75f);
        }
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

    m_res->blackBrush->SetOpacity(0.72f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(center, 3.2f, 3.2f), m_res->blackBrush);
    m_res->whiteBrush->SetOpacity(0.42f * alpha);
    ctx->DrawEllipse(D2D1::Ellipse(center, labelRadius, labelRadius), m_res->whiteBrush, 0.8f);
    m_res->whiteBrush->SetOpacity(0.22f * alpha);
    ctx->DrawEllipse(D2D1::Ellipse(center, radius - 1.0f, radius - 1.0f), m_res->whiteBrush, 0.9f);
}

void MusicPlayerComponent::RenderVinylTonearm(float left, float top, float size, float alpha) {
    auto* ctx = m_res->d2dContext;
    if (!ctx || !m_res->whiteBrush || !m_res->grayBrush) {
        return;
    }

    const D2D1_POINT_2F pivot = D2D1::Point2F(left + size * 0.92f, top + size * 0.15f);
    const D2D1_POINT_2F elbow = D2D1::Point2F(left + size * 1.12f, top + size * 0.27f);
    const D2D1_POINT_2F needle = D2D1::Point2F(left + size * 0.72f, top + size * 0.58f);
    const D2D1_POINT_2F counter = D2D1::Point2F(left + size * 1.02f, top + size * 0.06f);

    m_res->blackBrush->SetOpacity(0.40f * alpha);
    ctx->DrawLine(D2D1::Point2F(pivot.x + 1.0f, pivot.y + 1.0f), D2D1::Point2F(elbow.x + 1.0f, elbow.y + 1.0f), m_res->blackBrush, 3.4f);
    ctx->DrawLine(D2D1::Point2F(elbow.x + 1.0f, elbow.y + 1.0f), D2D1::Point2F(needle.x + 1.0f, needle.y + 1.0f), m_res->blackBrush, 3.0f);
    m_res->grayBrush->SetOpacity(0.62f * alpha);
    ctx->DrawLine(pivot, elbow, m_res->grayBrush, 2.8f);
    m_res->whiteBrush->SetOpacity(0.76f * alpha);
    ctx->DrawLine(elbow, needle, m_res->whiteBrush, 2.2f);
    m_res->grayBrush->SetOpacity(0.36f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(counter, size * 0.045f, size * 0.035f), m_res->grayBrush);
    m_res->grayBrush->SetOpacity(0.45f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(pivot, size * 0.085f, size * 0.085f), m_res->grayBrush);
    m_res->blackBrush->SetOpacity(0.72f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(pivot, size * 0.040f, size * 0.040f), m_res->blackBrush);
    m_res->whiteBrush->SetOpacity(0.70f * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(needle.x - size * 0.035f, needle.y - size * 0.025f, needle.x + size * 0.040f, needle.y + size * 0.030f), 2.0f, 2.0f), m_res->whiteBrush);
    m_res->blackBrush->SetOpacity(0.78f * alpha);
    ctx->DrawLine(needle, D2D1::Point2F(needle.x + size * 0.035f, needle.y + size * 0.070f), m_res->blackBrush, 1.2f);
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
