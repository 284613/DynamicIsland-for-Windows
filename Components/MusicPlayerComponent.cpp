// MusicPlayerComponent.cpp
#include "MusicPlayerComponent.h"
#include <dcomp.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>

MusicPlayerComponent::MusicPlayerComponent() {
}

MusicPlayerComponent::~MusicPlayerComponent() {
}

bool MusicPlayerComponent::Initialize() {
    return true;
}

void MusicPlayerComponent::SetD2DResources(
    ComPtr<ID2D1DeviceContext> d2dContext,
    ComPtr<IDWriteFactory> dwriteFactory,
    ComPtr<ID2D1Factory1> d2dFactory) {
    m_d2dContext = d2dContext;
    m_dwriteFactory = dwriteFactory;
    m_d2dFactory = d2dFactory;
}

void MusicPlayerComponent::SetTextFormats(
    ComPtr<IDWriteTextFormat> titleFormat,
    ComPtr<IDWriteTextFormat> subFormat,
    ComPtr<IDWriteTextFormat> iconFormat) {
    m_titleFormat = titleFormat;
    m_subFormat = subFormat;
    m_iconFormat = iconFormat;
}

void MusicPlayerComponent::SetBrushes(
    ComPtr<ID2D1SolidColorBrush> whiteBrush,
    ComPtr<ID2D1SolidColorBrush> grayBrush,
    ComPtr<ID2D1SolidColorBrush> themeBrush,
    ComPtr<ID2D1SolidColorBrush> progressBgBrush,
    ComPtr<ID2D1SolidColorBrush> progressFgBrush,
    ComPtr<ID2D1SolidColorBrush> buttonHoverBrush) {
    m_whiteBrush = whiteBrush;
    m_grayBrush = grayBrush;
    m_themeBrush = themeBrush;
    m_progressBgBrush = progressBgBrush;
    m_progressFgBrush = progressFgBrush;
    m_buttonHoverBrush = buttonHoverBrush;
}

void MusicPlayerComponent::SetAlbumBitmap(ComPtr<ID2D1Bitmap> bitmap) {
    m_albumBitmap = bitmap;
}

void MusicPlayerComponent::SetWicFactory(ComPtr<IWICImagingFactory> wicFactory) {
    m_wicFactory = wicFactory;
}

void MusicPlayerComponent::SetScrollState(float titleScrollOffset, float lyricScrollOffset,
    bool titleScrolling, bool lyricScrolling) {
    m_titleScrollOffset = titleScrollOffset;
    m_lyricScrollOffset = lyricScrollOffset;
    m_titleScrolling = titleScrolling;
    m_lyricScrolling = lyricScrolling;
}

bool MusicPlayerComponent::LoadAlbumArt(const std::wstring& file) {
    if (!m_wicFactory || !m_d2dContext) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
        file.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );

    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    ComPtr<IWICFormatConverter> converter;
    m_wicFactory->CreateFormatConverter(&converter);

    converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0,
        WICBitmapPaletteTypeCustom
    );

    hr = m_d2dContext->CreateBitmapFromWicBitmap(
        converter.Get(),
        nullptr,
        &m_albumBitmap
    );

    return SUCCEEDED(hr);
}

bool MusicPlayerComponent::LoadAlbumArtFromMemory(const std::vector<uint8_t>* data, size_t size) {
    if (!m_wicFactory || !m_d2dContext || !data || size == 0) return false;

    ComPtr<IWICStream> stream;
    m_wicFactory->CreateStream(&stream);

    stream->InitializeFromMemory(
        const_cast<uint8_t*>(data->data()),
        static_cast<DWORD>(size)
    );

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_wicFactory->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );

    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    ComPtr<IWICFormatConverter> converter;
    m_wicFactory->CreateFormatConverter(&converter);

    converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0,
        WICBitmapPaletteTypeCustom
    );

    hr = m_d2dContext->CreateBitmapFromWicBitmap(
        converter.Get(),
        nullptr,
        &m_albumBitmap
    );

    return SUCCEEDED(hr);
}

void MusicPlayerComponent::Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
    const RenderContext& ctx_data, float dpi) {
    if (!ctx) return;
    m_d2dContext = ctx;

    float right = left + width;
    bool compactMode = (ctx_data.islandHeight >= 35.0f && ctx_data.islandHeight < COMPACT_THRESHOLD);

    if (compactMode) {
        RenderCompact(left, top, width, height, ctx_data);
    }
    else {
        RenderExpanded(left, top, width, height, ctx_data);
    }
}

void MusicPlayerComponent::RenderExpanded(float left, float top, float width, float height, const RenderContext& ctx_data) {
    if (!m_d2dContext) return;

    float right = left + width;
    float artSize = ALBUM_ART_SIZE;
    float artLeft = left + ALBUM_ART_MARGIN;
    float artTop = top + 30.0f;

    float textLeft = artLeft + artSize + 15.0f;
    float textRight = right - 20.0f;
    float titleMaxWidth = textRight - textLeft;

    // ---------- 有媒体会话：显示音乐播放界面 ----------
    if (ctx_data.hasSession) {
        // 绘制专辑封面
        RenderAlbumArt(artLeft, artTop, artSize, ctx_data.contentAlpha);

        // 显示歌词（在歌名和歌手上面，左对齐）
        if (!ctx_data.lyric.empty()) {
            float lyricLeft = artLeft + artSize + 15.0f;
            float lyricWidth = textRight - lyricLeft;
            RenderLyrics(lyricLeft, artTop, lyricWidth, ctx_data.lyric, ctx_data.contentAlpha);
        }

        // 绘制"歌名 - 歌手"（在歌词下方，左对齐）
        std::wstring combinedText = ctx_data.title;
        if (!ctx_data.artist.empty()) {
            combinedText += L" - ";
            combinedText += ctx_data.artist;
        }

        float combinedWidth = titleMaxWidth - 60.0f;
        if (combinedWidth > 0 && m_subFormat) {
            // Create text layout directly
            ComPtr<IDWriteTextLayout> combinedLayout;
            HRESULT hr = m_dwriteFactory->CreateTextLayout(
                combinedText.c_str(),
                (UINT32)combinedText.length(),
                m_subFormat.Get(),
                combinedWidth,
                100.0f,
                &combinedLayout
            );

            if (SUCCEEDED(hr) && combinedLayout) {
                combinedLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

                // Set ellipsis trimming
                DWRITE_TRIMMING trimming{};
                trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
                ComPtr<IDWriteInlineObject> ellipsis;
                m_dwriteFactory->CreateEllipsisTrimmingSign(combinedLayout.Get(), &ellipsis);
                combinedLayout->SetTrimming(&trimming, ellipsis.Get());

                m_grayBrush->SetOpacity(ctx_data.contentAlpha);
                m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 25.0f), combinedLayout.Get(), m_grayBrush.Get());
            }
        }

        // 绘制播放波形（仅在播放时）
        if (ctx_data.isPlaying) {
            // Placeholder - would need wave height data passed in
        }

        // 绘制进度条
        float artistBottom = artTop + 60.0f;
        float progressBarY = artistBottom + 20.0f;
        float progressBarHeight = 6.0f;
        float progressBarLeft = textLeft - 80.0f;
        float progressBarRight = textLeft + titleMaxWidth;

        RenderProgressBar(progressBarLeft, progressBarY, progressBarRight - progressBarLeft, progressBarHeight,
            ctx_data.progress, ctx_data.contentAlpha, ctx_data.hoveredProgress, ctx_data.pressedProgress);

        // 绘制播放控制按钮
        float buttonY = progressBarY + progressBarHeight + 10.0f;
        float buttonGroupWidth = BUTTON_SIZE * 3 + BUTTON_SPACING * 2;
        float buttonX = textLeft + (titleMaxWidth - buttonGroupWidth) / 2.0f - 45.0f;
        if (buttonX < textLeft) buttonX = textLeft;
        RenderPlaybackButtons(buttonX, buttonY, BUTTON_SIZE, ctx_data.contentAlpha, ctx_data.isPlaying, -1, -1);
    }
}

void MusicPlayerComponent::RenderCompact(float left, float top, float width, float height, const RenderContext& ctx_data) {
    if (!m_d2dContext) return;

    float right = left + width;

    // 显示时间（暂停也显示时间）
    if (ctx_data.showTime && !ctx_data.timeText.empty() && !ctx_data.isPlaying && ctx_data.islandHeight >= 35.0f) {
        RenderCompactTime(ctx_data.timeText, left, top, width, height, ctx_data.contentAlpha);
    }
    // 显示音频可视化 + 滚动文本（播放时）
    else if (ctx_data.isPlaying) {
        float waveRight = right - 20.0f;
        float textLeft = left + 15.0f;
        float textRight = waveRight - 30.0f;

        std::wstring fullText = ctx_data.title;
        RenderCompactText(fullText, textLeft, top, textRight, height, ctx_data.contentAlpha);
    }
}

void MusicPlayerComponent::RenderAlbumArt(float left, float top, float size, float alpha) {
    if (!m_d2dContext) return;

    D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(
        D2D1::RectF(left, top, left + size, top + size),
        12.0f, 12.0f);

    if (m_albumBitmap) {
        m_d2dContext->DrawBitmap(m_albumBitmap.Get(), artRect.rect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else {
        m_themeBrush->SetOpacity(alpha);
        m_d2dContext->FillRoundedRectangle(&artRect, m_themeBrush.Get());
    }
}

void MusicPlayerComponent::RenderProgressBar(float left, float top, float width, float height,
    float progress, float alpha, int hoveredProgress, int pressedProgress) {
    if (!m_d2dContext || width <= 0 || height <= 0) return;

    float radius = height / 2.0f;

    // Hover effect
    float actualHeight = height;
    float actualTop = top;
    if (hoveredProgress != -1 || pressedProgress != -1) {
        actualHeight = 10.0f;
        actualTop = top - 2.0f;
    }

    // Background
    if (m_progressBgBrush) {
        m_progressBgBrush->SetOpacity(0.5f * alpha);
        D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(
            D2D1::RectF(left, actualTop, left + width, actualTop + actualHeight),
            radius, radius
        );
        m_d2dContext->FillRoundedRectangle(&bgRect, m_progressBgBrush.Get());
    }

    // Progress foreground
    if (m_progressFgBrush && progress > 0) {
        m_progressFgBrush->SetOpacity(alpha);
        float progressWidth = width * progress;
        D2D1_ROUNDED_RECT fgRect = D2D1::RoundedRect(
            D2D1::RectF(left, actualTop, left + progressWidth, actualTop + actualHeight),
            radius, radius
        );
        m_d2dContext->FillRoundedRectangle(&fgRect, m_progressFgBrush.Get());
    }
}

void MusicPlayerComponent::RenderPlaybackButtons(float left, float top, float buttonSize, float alpha, bool isPlaying,
    int hoveredButton, int pressedButton) {
    if (!m_d2dContext) return;

    const wchar_t prevIcon = L'\uE892';
    const wchar_t playIcon = L'\uE768';
    const wchar_t pauseIcon = L'\uE769';
    const wchar_t nextIcon = L'\uE893';

    m_whiteBrush->SetOpacity(alpha);

    // Previous button
    D2D1_RECT_F prevRect = D2D1::RectF(left, top, left + buttonSize, top + buttonSize);
    m_d2dContext->DrawTextW(&prevIcon, 1, m_iconFormat.Get(), prevRect, m_whiteBrush.Get());

    // Play/Pause button
    float playX = left + buttonSize + BUTTON_SPACING;
    D2D1_RECT_F playRect = D2D1::RectF(playX, top, playX + buttonSize, top + buttonSize);
    const wchar_t* playIconPtr = isPlaying ? &pauseIcon : &playIcon;
    m_d2dContext->DrawTextW(playIconPtr, 1, m_iconFormat.Get(), playRect, m_whiteBrush.Get());

    // Next button
    float nextX = left + (buttonSize + BUTTON_SPACING) * 2;
    D2D1_RECT_F nextRect = D2D1::RectF(nextX, top, nextX + buttonSize, top + buttonSize);
    m_d2dContext->DrawTextW(&nextIcon, 1, m_iconFormat.Get(), nextRect, m_whiteBrush.Get());
}

void MusicPlayerComponent::RenderLyrics(float left, float top, float width, const std::wstring& lyric, float alpha) {
    if (!m_d2dContext || lyric.empty() || !m_titleFormat) return;

    // Create text layout directly
    ComPtr<IDWriteTextLayout> lyricLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        lyric.c_str(),
        (UINT32)lyric.length(),
        m_titleFormat.Get(),
        width,
        100.0f,
        &lyricLayout
    );

    if (!lyricLayout) return;

    lyricLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    lyricLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    DWRITE_TEXT_METRICS metrics;
    lyricLayout->GetMetrics(&metrics);

    bool needScroll = (metrics.width > width);
    if (needScroll != m_lyricScrolling || lyric != m_lastDrawnLyric) {
        m_lyricScrolling = needScroll;
        if (lyric != m_lastDrawnLyric) {
            m_lyricScrollOffset = 0.0f;
            m_lastDrawnLyric = lyric;
        }
    }

    D2D1_RECT_F clipRect = D2D1::RectF(left, top, left + width, top + 25.0f);
    m_d2dContext->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    m_whiteBrush->SetOpacity(alpha);

    if (needScroll) {
        float offset = fmod(m_lyricScrollOffset, metrics.width);
        m_d2dContext->DrawTextLayout(D2D1::Point2F(left - offset, top), lyricLayout.Get(), m_whiteBrush.Get());
        m_d2dContext->DrawTextLayout(D2D1::Point2F(left - offset + metrics.width, top), lyricLayout.Get(), m_whiteBrush.Get());
    }
    else {
        m_d2dContext->DrawTextLayout(D2D1::Point2F(left, top), lyricLayout.Get(), m_whiteBrush.Get());
    }

    m_d2dContext->PopAxisAlignedClip();
}

void MusicPlayerComponent::RenderCompactText(const std::wstring& fullText, float left, float top,
    float textRight, float islandHeight, float alpha) {
    if (!m_d2dContext || fullText.empty() || !m_titleFormat) return;

    float maxWidth = textRight - left;
    if (maxWidth <= 0) return;

    // Create text layout directly
    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        fullText.c_str(),
        (UINT32)fullText.length(),
        m_titleFormat.Get(),
        maxWidth,
        100.0f,
        &textLayout
    );

    if (!textLayout) return;

    textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    bool needScroll = (metrics.width > maxWidth);
    if (needScroll != m_titleScrolling || fullText != m_lastDrawnFullText) {
        m_titleScrolling = needScroll;
        if (fullText != m_lastDrawnFullText) {
            m_titleScrollOffset = 0.0f;
            m_lastDrawnFullText = fullText;
        }
    }

    float textHeight = metrics.height;
    float yPos = top + (islandHeight - textHeight) / 2.0f;

    m_whiteBrush->SetOpacity(alpha);
    m_d2dContext->PushAxisAlignedClip(D2D1::RectF(left, top, textRight, top + islandHeight), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (needScroll) {
        float offset = fmod(m_titleScrollOffset, metrics.width);
        m_d2dContext->DrawTextLayout(D2D1::Point2F(left - offset, yPos), textLayout.Get(), m_whiteBrush.Get());
        m_d2dContext->DrawTextLayout(D2D1::Point2F(left - offset + metrics.width, yPos), textLayout.Get(), m_whiteBrush.Get());
    }
    else {
        float textOffset = (maxWidth - metrics.width) / 2.0f;
        m_d2dContext->DrawTextLayout(D2D1::Point2F(left + textOffset, yPos), textLayout.Get(), m_whiteBrush.Get());
    }

    m_d2dContext->PopAxisAlignedClip();
}

void MusicPlayerComponent::RenderCompactTime(const std::wstring& timeText, float left, float top,
    float islandWidth, float islandHeight, float alpha) {
    if (!m_d2dContext || timeText.empty() || !m_titleFormat) return;

    // Create text layout directly
    ComPtr<IDWriteTextLayout> timeLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        timeText.c_str(),
        (UINT32)timeText.length(),
        m_titleFormat.Get(),
        200.0f,
        100.0f,
        &timeLayout
    );

    if (!timeLayout) return;

    DWRITE_TEXT_METRICS metrics;
    timeLayout->GetMetrics(&metrics);

    float textX = left + (islandWidth - metrics.width) / 2.0f;
    float textY = top + (islandHeight - metrics.height) / 2.0f;

    m_whiteBrush->SetOpacity(alpha);
    m_d2dContext->DrawTextLayout(D2D1::Point2F(textX, textY), timeLayout.Get(), m_whiteBrush.Get());
}