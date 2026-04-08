// VolumeComponent.cpp
#include "VolumeComponent.h"
#include <dcomp.h>

VolumeComponent::VolumeComponent() {
}

VolumeComponent::~VolumeComponent() {
}

bool VolumeComponent::Initialize() {
    return true;
}

void VolumeComponent::SetD2DResources(
    ComPtr<ID2D1DeviceContext> d2dContext,
    ComPtr<IDWriteFactory> dwriteFactory) {
    m_d2dContext = d2dContext;
    m_dwriteFactory = dwriteFactory;
}

void VolumeComponent::SetBrushes(
    ComPtr<ID2D1SolidColorBrush> whiteBrush,
    ComPtr<ID2D1SolidColorBrush> grayBrush) {
    m_whiteBrush = whiteBrush;
    m_grayBrush = grayBrush;
}

void VolumeComponent::SetIconFormat(ComPtr<IDWriteTextFormat> iconFormat) {
    m_iconFormat = iconFormat;
}

const wchar_t* VolumeComponent::GetVolumeIcon(float volumeLevel) {
    if (volumeLevel <= 0.0f) return L"\uE74F";      // Mute
    else if (volumeLevel <= 0.35f) return L"\uE992"; // Low
    else if (volumeLevel <= 0.65f) return L"\uE993"; // Medium
    else return L"\uE994";                           // High
}

void VolumeComponent::Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
    const RenderContext& ctx_data, float dpi) {
    if (!ctx) return;
    m_d2dContext = ctx;

    // Calculate positions
    float totalWidth = ICON_SIZE + 15.0f + BAR_WIDTH;
    float startX = left + (width - totalWidth) / 2.0f;

    // Draw volume icon
    const wchar_t* volIcon = GetVolumeIcon(ctx_data.volumeLevel);
    D2D1_RECT_F iconRect = D2D1::RectF(startX, top, startX + ICON_SIZE, top + height);
    m_d2dContext->DrawTextW(volIcon, 1, m_iconFormat.Get(), iconRect, m_whiteBrush.Get());

    // Draw background bar
    float barY = top + (height - BAR_HEIGHT) / 2.0f;
    float barX = startX + ICON_SIZE + 15.0f;

    m_grayBrush->SetOpacity(0.5f * ctx_data.contentAlpha);
    m_d2dContext->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + BAR_WIDTH, barY + BAR_HEIGHT), BAR_HEIGHT / 2.0f, BAR_HEIGHT / 2.0f),
        m_grayBrush.Get());

    // Draw volume foreground
    if (ctx_data.volumeLevel > 0.0f) {
        m_whiteBrush->SetOpacity(ctx_data.contentAlpha);
        m_d2dContext->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + BAR_WIDTH * ctx_data.volumeLevel, barY + BAR_HEIGHT), BAR_HEIGHT / 2.0f, BAR_HEIGHT / 2.0f),
            m_whiteBrush.Get());
    }
}

void VolumeComponent::RenderOSD(float canvasWidth, float canvasHeight, float volumeLevel, float alpha) {
    // Placeholder for custom volume OSD overlay
    // Requires intercepting Windows volume notifications
}