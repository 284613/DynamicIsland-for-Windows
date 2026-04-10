#include "components/AlertComponent.h"

void AlertComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void AlertComponent::SetAlertState(bool active, const AlertInfo& info) {
    m_isAlertActive = active;
    m_alert = info;
    if (!active) {
        m_alertBitmap.Reset();
    }
}

bool AlertComponent::LoadAlertIcon(const std::wstring& file) {
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

    m_alertBitmap.Reset();
    return SUCCEEDED(m_res->d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_alertBitmap));
}

bool AlertComponent::LoadAlertIconFromMemory(const std::vector<uint8_t>& data) {
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

    m_alertBitmap.Reset();
    return SUCCEEDED(m_res->d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_alertBitmap));
}

const wchar_t* AlertComponent::GetIconText(int alertType) {
    switch (alertType) {
    case 1: return L"\uE701";
    case 2: return L"\uE702";
    case 4: return L"\uEBB5";
    case 5: return L"\uEBAE";
    case 6: return L"\uE8A5";
    default: return L"\uE7E7";
    }
}

ID2D1SolidColorBrush* AlertComponent::GetBrush(int alertType) {
    if (!m_res) return nullptr;
    switch (alertType) {
    case 1: return m_res->wifiBrush;
    case 2: return m_res->bluetoothBrush;
    case 4: return m_res->chargingBrush;
    case 5: return m_res->lowBatteryBrush;
    case 6: return m_res->fileBrush;
    default: return m_res->notificationBrush;
    }
}

ComPtr<IDWriteTextLayout> AlertComponent::GetOrCreateTextLayout(
    const std::wstring& text, IDWriteTextFormat* format, float maxWidth, const std::wstring& cacheKey) {
    auto it = m_textLayoutCache.find(cacheKey);
    if (it != m_textLayoutCache.end() && it->second.text == text && it->second.maxWidth == maxWidth) {
        return it->second.layout;
    }

    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(), format, maxWidth, 100.0f, &layout);
    if (layout) {
        m_textLayoutCache[cacheKey] = { layout, text, maxWidth };
    }
    return layout;
}

void AlertComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_isAlertActive) return;

    float height = rect.bottom - rect.top;
    if (height < COMPACT_THRESHOLD) {
        RenderCompact(rect, contentAlpha);
    } else {
        RenderExpanded(rect, contentAlpha);
    }
}

void AlertComponent::RenderCompact(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;

    bool isAppNotif = (m_alert.type == 3);
    const wchar_t* iconText = GetIconText(m_alert.type);
    auto* iconBrush = GetBrush(m_alert.type);

    std::wstring text;
    if (isAppNotif) text = m_alert.name + L" 有新消息";
    else if (m_alert.type == 1) text = L"Wi-Fi 已连接";
    else if (m_alert.type == 2) text = m_alert.deviceType + L" 已连接";
    else if (m_alert.type == 4 || m_alert.type == 5) text = m_alert.name + L" (" + m_alert.deviceType + L")";

    auto layout = GetOrCreateTextLayout(text, m_res->titleFormat, 10000.0f, L"alert_compact_text_" + text);
    if (!layout) return;

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);

    float totalWidth = ICON_SIZE + 8.0f + metrics.width;
    float startX = left + (width - totalWidth) / 2.0f;
    float textY = top + (height - metrics.height) / 2.0f;
    D2D1_RECT_F iconRect = D2D1::RectF(startX, top + (height - ICON_SIZE) / 2.0f,
        startX + ICON_SIZE, top + (height - ICON_SIZE) / 2.0f + ICON_SIZE);

    if (isAppNotif && m_alertBitmap) {
        ctx->DrawBitmap(m_alertBitmap.Get(), iconRect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else if (isAppNotif && m_res->themeBrush) {
        m_res->themeBrush->SetOpacity(contentAlpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(iconRect, 4.0f, 4.0f), m_res->themeBrush);
    } else if (!isAppNotif && iconBrush) {
        iconBrush->SetOpacity(contentAlpha);
        ctx->DrawTextW(iconText, 1, m_res->iconFormat, D2D1::RectF(startX, top, startX + ICON_SIZE, rect.bottom), iconBrush);
    }

    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextLayout(D2D1::Point2F(startX + ICON_SIZE + 8.0f, textY), layout.Get(), m_res->whiteBrush);
}

void AlertComponent::RenderExpanded(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float right = rect.right;
    float bottom = rect.bottom;

    bool isAppNotif = (m_alert.type == 3);
    const wchar_t* iconText = GetIconText(m_alert.type);
    auto* iconBrush = GetBrush(m_alert.type);

    float artLeft = left + 20.0f;
    float artTop = top + 30.0f;
    D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(artLeft, artTop, artLeft + ART_SIZE, artTop + ART_SIZE), 12.0f, 12.0f);

    if (isAppNotif && m_alertBitmap) {
        ctx->DrawBitmap(m_alertBitmap.Get(), artRect.rect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        if (iconBrush) {
            iconBrush->SetOpacity(contentAlpha);
            ctx->FillRoundedRectangle(&artRect, iconBrush);
        }
        if (!isAppNotif) {
            m_res->whiteBrush->SetOpacity(contentAlpha);
            ctx->DrawTextW(iconText, 1, m_res->iconFormat, artRect.rect, m_res->whiteBrush);
        }
    }

    float textLeft = artLeft + ART_SIZE + 15.0f;
    float textRight = right - 20.0f;
    std::wstring topText;
    std::wstring bottomText;

    if (isAppNotif) {
        topText = m_alert.name;
        bottomText = m_alert.deviceType;
    } else if (m_alert.type == 1) {
        topText = L"Wi-Fi";
        bottomText = m_alert.name;
    } else if (m_alert.type == 2) {
        topText = m_alert.deviceType;
        bottomText = m_alert.name;
    } else if (m_alert.type == 4 || m_alert.type == 5) {
        topText = m_alert.deviceType;
        bottomText = m_alert.name;
    }

    auto topLayout = GetOrCreateTextLayout(topText, m_res->subFormat, 10000.0f, L"alert_top_" + topText);
    if (topLayout) {
        m_res->grayBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 5.0f), topLayout.Get(), m_res->grayBrush);
    }

    if (!bottomText.empty()) {
        auto botLayout = GetOrCreateTextLayout(bottomText, m_res->titleFormat, 10000.0f, L"alert_bottom_" + bottomText);
        if (botLayout) {
            botLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_res->whiteBrush->SetOpacity(contentAlpha);
            ctx->PushAxisAlignedClip(D2D1::RectF(textLeft, artTop + 25.0f, textRight, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            ctx->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 28.0f), botLayout.Get(), m_res->whiteBrush);
            ctx->PopAxisAlignedClip();
        }
    }
}
