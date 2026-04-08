// AlertComponent.h
#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "IslandState.h"

using namespace Microsoft::WRL;

class AlertComponent {
public:
    AlertComponent();
    ~AlertComponent();

    bool Initialize();

    void SetD2DResources(ComPtr<ID2D1DeviceContext> d2dContext, ComPtr<IDWriteFactory> dwriteFactory);
    
    void SetBrushes(
        ComPtr<ID2D1SolidColorBrush> wifiBrush,
        ComPtr<ID2D1SolidColorBrush> bluetoothBrush,
        ComPtr<ID2D1SolidColorBrush> chargingBrush,
        ComPtr<ID2D1SolidColorBrush> lowBatteryBrush,
        ComPtr<ID2D1SolidColorBrush> fileBrush,
        ComPtr<ID2D1SolidColorBrush> notificationBrush,
        ComPtr<ID2D1SolidColorBrush> whiteBrush,
        ComPtr<ID2D1SolidColorBrush> darkGrayBrush,
        ComPtr<ID2D1SolidColorBrush> themeBrush,
        ComPtr<ID2D1SolidColorBrush> grayBrush);

    void SetTextFormat(ComPtr<IDWriteTextFormat> textFormat);
    void SetTitleTextFormat(ComPtr<IDWriteTextFormat> titleFormat);
    void SetIconFormat(ComPtr<IDWriteTextFormat> iconFormat);

    void SetAlertState(bool active, int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType);
    void SetAlertBitmap(ComPtr<ID2D1Bitmap> bitmap);

    void Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
              const RenderContext& ctx_data, float dpi);

private:
    const wchar_t* GetIconText(int alertType);
    ComPtr<ID2D1SolidColorBrush> GetBrush(int alertType);

    struct TextLayoutCacheEntry {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        float maxWidth;
    };

    ComPtr<IDWriteTextLayout> GetOrCreateTextLayout(
        const std::wstring& text, IDWriteTextFormat* format, float maxWidth, const std::wstring& cacheKey);

    void RenderCompact(float left, float top, float width, float height, const RenderContext& ctx_data,
        int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType,
        bool isAppNotif, const wchar_t* iconText, ComPtr<ID2D1SolidColorBrush> iconBrush, float contentAlpha);

    void RenderExpanded(float left, float top, float width, float height, const RenderContext& ctx_data,
        int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType,
        bool isAppNotif, const wchar_t* iconText, ComPtr<ID2D1SolidColorBrush> iconBrush, float contentAlpha);

    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<IDWriteFactory> m_dwriteFactory;

    ComPtr<ID2D1SolidColorBrush> m_wifiBrush;
    ComPtr<ID2D1SolidColorBrush> m_bluetoothBrush;
    ComPtr<ID2D1SolidColorBrush> m_chargingBrush;
    ComPtr<ID2D1SolidColorBrush> m_lowBatteryBrush;
    ComPtr<ID2D1SolidColorBrush> m_fileBrush;
    ComPtr<ID2D1SolidColorBrush> m_notificationBrush;
    ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    ComPtr<ID2D1SolidColorBrush> m_darkGrayBrush;
    ComPtr<ID2D1SolidColorBrush> m_themeBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;

    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextFormat> m_titleFormat;
    ComPtr<IDWriteTextFormat> m_iconFormat;

    bool m_isAlertActive = false;
    int m_alertType = 0;
    std::wstring m_alertName;
    std::wstring m_alertDeviceType;
    ComPtr<ID2D1Bitmap> m_alertBitmap;

    std::unordered_map<std::wstring, TextLayoutCacheEntry> m_textLayoutCache;

    static constexpr float ICON_SIZE = 24.0f;
    static constexpr float ART_SIZE = 60.0f;
};