#include "components/AlertComponent.h"

AlertComponent::AlertComponent() {}
AlertComponent::~AlertComponent() {}

bool AlertComponent::Initialize() { return true; }

void AlertComponent::SetD2DResources(ComPtr<ID2D1DeviceContext> d2dContext, ComPtr<IDWriteFactory> dwriteFactory) {
    m_d2dContext = d2dContext;
    m_dwriteFactory = dwriteFactory;
}

void AlertComponent::SetBrushes(
    ComPtr<ID2D1SolidColorBrush> wifiBrush,
    ComPtr<ID2D1SolidColorBrush> bluetoothBrush,
    ComPtr<ID2D1SolidColorBrush> chargingBrush,
    ComPtr<ID2D1SolidColorBrush> lowBatteryBrush,
    ComPtr<ID2D1SolidColorBrush> fileBrush,
    ComPtr<ID2D1SolidColorBrush> notificationBrush,
    ComPtr<ID2D1SolidColorBrush> whiteBrush,
    ComPtr<ID2D1SolidColorBrush> darkGrayBrush,
    ComPtr<ID2D1SolidColorBrush> themeBrush,
    ComPtr<ID2D1SolidColorBrush> grayBrush) {
    m_wifiBrush = wifiBrush;
    m_bluetoothBrush = bluetoothBrush;
    m_chargingBrush = chargingBrush;
    m_lowBatteryBrush = lowBatteryBrush;
    m_fileBrush = fileBrush;
    m_notificationBrush = notificationBrush;
    m_whiteBrush = whiteBrush;
    m_darkGrayBrush = darkGrayBrush;
    m_themeBrush = themeBrush;
    m_grayBrush = grayBrush;
}

void AlertComponent::SetTextFormat(ComPtr<IDWriteTextFormat> textFormat) { m_textFormat = textFormat; }
void AlertComponent::SetTitleTextFormat(ComPtr<IDWriteTextFormat> titleFormat) { m_titleFormat = titleFormat; }
void AlertComponent::SetIconFormat(ComPtr<IDWriteTextFormat> iconFormat) { m_iconFormat = iconFormat; }

void AlertComponent::SetAlertState(bool active, int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType) {
    m_isAlertActive = active;
    m_alertType = alertType;
    m_alertName = alertName;
    m_alertDeviceType = alertDeviceType;
}

void AlertComponent::SetAlertBitmap(ComPtr<ID2D1Bitmap> bitmap) { m_alertBitmap = bitmap; }

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

ComPtr<ID2D1SolidColorBrush> AlertComponent::GetBrush(int alertType) {
    switch (alertType) {
        case 1: return m_wifiBrush;
        case 2: return m_bluetoothBrush;
        case 4: return m_chargingBrush;
        case 5: return m_lowBatteryBrush;
        case 6: return m_fileBrush;
        default: return m_notificationBrush;
    }
}

ComPtr<IDWriteTextLayout> AlertComponent::GetOrCreateTextLayout(
    const std::wstring& text, IDWriteTextFormat* format, float maxWidth, const std::wstring& cacheKey) {
    auto it = m_textLayoutCache.find(cacheKey);
    if (it != m_textLayoutCache.end() && it->second.text == text) {
        return it->second.layout;
    }
    ComPtr<IDWriteTextLayout> textLayout;
    m_dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(), format, maxWidth, 100.0f, &textLayout);
    if (textLayout) {
        TextLayoutCacheEntry entry;
        entry.layout = textLayout;
        entry.text = text;
        entry.maxWidth = maxWidth;
        m_textLayoutCache[cacheKey] = entry;
    }
    return textLayout;
}

void AlertComponent::Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
    const RenderContext& ctx_data, float dpi) {
    if (!ctx || !ctx_data.isAlertActive) return;
    m_d2dContext = ctx;

    bool isAppNotif = (ctx_data.alertType == 3);
    const wchar_t* iconText = GetIconText(ctx_data.alertType);
    ComPtr<ID2D1SolidColorBrush> iconBrush = GetBrush(ctx_data.alertType);

    float compactThreshold = 60.0f;
    if (height < compactThreshold) {
        RenderCompact(left, top, width, height, ctx_data, ctx_data.alertType,
            ctx_data.alertName, ctx_data.alertDeviceType, isAppNotif, iconText, iconBrush, ctx_data.contentAlpha);
    } else {
        RenderExpanded(left, top, width, height, ctx_data, ctx_data.alertType,
            ctx_data.alertName, ctx_data.alertDeviceType, isAppNotif, iconText, iconBrush, ctx_data.contentAlpha);
    }
}

void AlertComponent::RenderCompact(float left, float top, float width, float height, const RenderContext& ctx_data,
    int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType,
    bool isAppNotif, const wchar_t* iconText, ComPtr<ID2D1SolidColorBrush> iconBrush, float contentAlpha) {
    float iconSize = ICON_SIZE;
    std::wstring text = L"";

    if (isAppNotif) {
        text = alertName + L" \u6709\u65b0\u6d88\u606f";
    } else if (alertType == 1) {
        text = L"Wi-Fi \u5df2\u8fde\u63a5";
    } else if (alertType == 2) {
        text = alertDeviceType + L" \u5df2\u8fde\u63a5";
    } else if (alertType == 4 || alertType == 5) {
        text = alertName + L" (";
        text += alertDeviceType;
        text += L")";
    }

    auto textLayout = GetOrCreateTextLayout(text, m_titleFormat.Get(), 10000.0f, L"alert_compact_text");
    if (!textLayout) return;
    
    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    float totalWidth = iconSize + 8.0f + metrics.width;
    float startX = left + (width - totalWidth) / 2.0f;
    float textY = top + (height - metrics.height) / 2.0f;

    D2D1_RECT_F iconRect = D2D1::RectF(startX, top + (height - iconSize) / 2.0f, startX + iconSize, top + (height - iconSize) / 2.0f + iconSize);
    if (isAppNotif && m_alertBitmap) {
        m_d2dContext->DrawBitmap(m_alertBitmap.Get(), iconRect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else if (!isAppNotif) {
        if (iconBrush) iconBrush->SetOpacity(contentAlpha);
        m_d2dContext->DrawTextW(iconText, 1, m_iconFormat.Get(), iconRect, iconBrush.Get());
    }

    if (m_whiteBrush) m_whiteBrush->SetOpacity(contentAlpha);
    m_d2dContext->DrawTextLayout(D2D1::Point2F(startX + iconSize + 8.0f, textY), textLayout.Get(), m_whiteBrush.Get());
}

void AlertComponent::RenderExpanded(float left, float top, float width, float height, const RenderContext& ctx_data,
    int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType,
    bool isAppNotif, const wchar_t* iconText, ComPtr<ID2D1SolidColorBrush> iconBrush, float contentAlpha) {
    float artSize = ART_SIZE;
    float artLeft = left + 20.0f;
    float artTop = top + 30.0f;
    D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), 12.0f, 12.0f);

    if (isAppNotif && m_alertBitmap) {
        m_d2dContext->DrawBitmap(m_alertBitmap.Get(), artRect.rect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        if (iconBrush) {
            iconBrush->SetOpacity(contentAlpha);
            m_d2dContext->FillRoundedRectangle(&artRect, iconBrush.Get());
        }
        if (!isAppNotif) {
            if (m_whiteBrush) m_whiteBrush->SetOpacity(contentAlpha);
            m_d2dContext->DrawTextW(iconText, 1, m_iconFormat.Get(), artRect.rect, m_whiteBrush.Get());
        }
    }

    float textLeft = artLeft + artSize + 15.0f;
    float textRight = left + width - 20.0f;
    std::wstring topText = L"";
    std::wstring bottomText = L"";

    if (isAppNotif) {
        topText = alertName;
        bottomText = alertDeviceType;
    } else if (alertType == 1) {
        topText = L"Wi-Fi";
        bottomText = alertName;
    } else if (alertType == 2) {
        topText = alertDeviceType;
        bottomText = alertName;
    } else if (alertType == 4 || alertType == 5) {
        topText = alertDeviceType;
        bottomText = alertName;
    }

    auto topLayout = GetOrCreateTextLayout(topText, m_textFormat.Get(), 10000.0f, L"alert_top");
    if (topLayout) {
        if (m_grayBrush) m_grayBrush->SetOpacity(contentAlpha);
        m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 5.0f), topLayout.Get(), m_grayBrush.Get());
    }

    if (!bottomText.empty()) {
        auto botLayout = GetOrCreateTextLayout(bottomText, m_titleFormat.Get(), 10000.0f, L"alert_bottom");
        if (botLayout) {
            botLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            if (m_whiteBrush) m_whiteBrush->SetOpacity(contentAlpha);
            m_d2dContext->PushAxisAlignedClip(D2D1::RectF(textLeft, artTop + 25.0f, textRight, top + height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 28.0f), botLayout.Get(), m_whiteBrush.Get());
            m_d2dContext->PopAxisAlignedClip();
        }
    }
}
