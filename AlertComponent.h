// AlertComponent.h
#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include "../IslandState.h"
#include "../Messages.h"

using namespace Microsoft::WRL;

class AlertComponent {
public:
    AlertComponent();
    ~AlertComponent();

    bool Initialize();

    // Set D2D resources
    void SetD2DResources(
        ComPtr<ID2D1DeviceContext> d2dContext,
        ComPtr<IDWriteFactory> dwriteFactory);

    // Set brushes
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

    // Set text format
    void SetTextFormat(ComPtr<IDWriteTextFormat> textFormat);

    // Set title text format (for larger text)
    void SetTitleTextFormat(ComPtr<IDWriteTextFormat> titleFormat);

    // Set icon format
    void SetIconFormat(ComPtr<IDWriteTextFormat> iconFormat);

    // Set alert state (alert data from RenderEngine)
    void SetAlertState(bool active, int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType);

    // Set alert icon bitmap
    void SetAlertBitmap(ComPtr<ID2D1Bitmap> bitmap);

    // Main Draw method - handles all alert rendering
    void Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
              const RenderContext& ctx_data, float dpi);

private:
    void RenderCompact(float left, float top, float width, float height, const RenderContext& ctx_data,
        int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType,
        bool isAppNotif, const wchar_t* iconText, ComPtr<ID2D1SolidColorBrush> iconBrush, float contentAlpha);
    void RenderExpanded(float left, float top, float width, float height, const RenderContext& ctx_data,
        int alertType, const std::wstring& alertName, const std::wstring& alertDeviceType,
        bool isAppNotif, const wchar_t* iconText, ComPtr<ID2D1SolidColorBrush> iconBrush, float contentAlpha);

    const wchar_t* GetIconText(int alertType);
    ComPtr<ID2D1SolidColorBrush> GetBrush(int alertType);

    // D2D resources
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<IDWriteFactory> m_dwriteFactory;

    // Brushes
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

    // Text formats
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextFormat> m_titleFormat;  // For larger title text
    ComPtr<IDWriteTextFormat> m_iconFormat;

    // Alert icon bitmap
    ComPtr<ID2D1Bitmap> m_alertBitmap;

    // Alert state
    bool m_isAlertActive = false;
    int m_alertType = 0;
    std::wstring m_alertName;
    std::wstring m_alertDeviceType;

    // TextLayout cache for alert text
    struct TextLayoutCacheEntry {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        float maxWidth;
    };
    std::unordered_map<std::wstring, TextLayoutCacheEntry> m_textLayoutCache;

    // Helper to get or create cached TextLayout
    ComPtr<IDWriteTextLayout> GetOrCreateTextLayout(
        const std::wstring& text,
        IDWriteTextFormat* format,
        float maxWidth,
        const std::wstring& cacheKey);

    // Constants
    static constexpr float ICON_SIZE = 20.0f;
    static constexpr float ART_SIZE = 60.0f;
};