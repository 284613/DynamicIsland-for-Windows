// FilePanelComponent.h
#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "IslandState.h"

using namespace Microsoft::WRL;

class FilePanelComponent {
public:
    FilePanelComponent();
    ~FilePanelComponent();

    bool Initialize();

    void SetD2DResources(
        ComPtr<ID2D1DeviceContext> d2dContext,
        ComPtr<IDWriteFactory> dwriteFactory,
        ComPtr<ID2D1Factory1> d2dFactory);

    void SetTextFormats(
        ComPtr<IDWriteTextFormat> titleFormat,
        ComPtr<IDWriteTextFormat> subFormat,
        ComPtr<IDWriteTextFormat> iconFormat,
        ComPtr<IDWriteTextFormat> iconTextFormat);

    void SetBrushes(
        ComPtr<ID2D1SolidColorBrush> whiteBrush,
        ComPtr<ID2D1SolidColorBrush> grayBrush,
        ComPtr<ID2D1SolidColorBrush> themeBrush,
        ComPtr<ID2D1SolidColorBrush> buttonHoverBrush,
        ComPtr<ID2D1SolidColorBrush> fileBrush);

    void SetWicFactory(ComPtr<IWICImagingFactory> wicFactory);

    void Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
              const RenderContext& ctx_data, float dpi);

private:
    void RenderCompactView(float left, float top, float width, float height, const RenderContext& ctx_data);
    void RenderFileList(float left, float top, float width, float height, const RenderContext& ctx_data);
    void DrawFileItem(float x, float y, float width, const std::wstring& path, 
                     bool hovered, bool deleteHovered, float alpha);
    
    ComPtr<ID2D1Bitmap> GetFileIcon(const std::wstring& path);
    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text, IDWriteTextFormat* format, float maxWidth);

    // D2D resources
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<IWICImagingFactory> m_wicFactory;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;
    ComPtr<ID2D1SolidColorBrush> m_themeBrush;
    ComPtr<ID2D1SolidColorBrush> m_buttonHoverBrush;
    ComPtr<ID2D1SolidColorBrush> m_fileBrush;

    // Text formats
    ComPtr<IDWriteTextFormat> m_titleFormat;
    ComPtr<IDWriteTextFormat> m_subFormat;
    ComPtr<IDWriteTextFormat> m_iconFormat;
    ComPtr<IDWriteTextFormat> m_iconTextFormat;

    // Icon cache
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> m_iconCache;

    // Layout constants
    static constexpr float ITEM_HEIGHT = 40.0f;
    static constexpr float ICON_SIZE = 22.0f;
    static constexpr float DELETE_BTN_SIZE = 22.0f;
    static constexpr float PADDING = 12.0f;
};