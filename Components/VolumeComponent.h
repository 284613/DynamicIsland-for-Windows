// VolumeComponent.h
#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <functional>
#include "../IslandState.h"

using namespace Microsoft::WRL;

class VolumeComponent {
public:
    VolumeComponent();
    ~VolumeComponent();

    bool Initialize();

    // Set D2D resources
    void SetD2DResources(
        ComPtr<ID2D1DeviceContext> d2dContext,
        ComPtr<IDWriteFactory> dwriteFactory);

    // Set brushes
    void SetBrushes(
        ComPtr<ID2D1SolidColorBrush> whiteBrush,
        ComPtr<ID2D1SolidColorBrush> grayBrush);

    // Set icon format
    void SetIconFormat(ComPtr<IDWriteTextFormat> iconFormat);

    // Main Draw method - handles volume bar rendering
    void Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
              const RenderContext& ctx_data, float dpi);

    // Render volume OSD (for future expansion)
    void RenderOSD(float canvasWidth, float canvasHeight, float volumeLevel, float alpha);

private:
    const wchar_t* GetVolumeIcon(float volumeLevel);

    // D2D resources
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<IDWriteFactory> m_dwriteFactory;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_whiteBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;

    // Icon format
    ComPtr<IDWriteTextFormat> m_iconFormat;

    // Constants
    static constexpr float ICON_SIZE = 20.0f;
    static constexpr float BAR_WIDTH = 120.0f;
    static constexpr float BAR_HEIGHT = 6.0f;
};