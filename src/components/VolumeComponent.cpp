#include "components/VolumeComponent.h"

void VolumeComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

const wchar_t* VolumeComponent::GetVolumeIcon(float level) {
    if (level <= 0.0f)    return L"\uE74F";  // Mute
    if (level <= 0.35f)   return L"\uE992";  // Low
    if (level <= 0.65f)   return L"\uE993";  // Medium
    return                       L"\uE994";  // High
}

void VolumeComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_isActive) return;
    auto* ctx = m_res->d2dContext;

    float left = rect.left, top = rect.top;
    float width = rect.right - rect.left, height = rect.bottom - rect.top;

    float totalWidth = ICON_SIZE + 15.0f + BAR_WIDTH;
    float startX = left + (width - totalWidth) / 2.0f;

    // Volume icon
    const wchar_t* volIcon = GetVolumeIcon(m_volumeLevel);
    ctx->DrawTextW(volIcon, 1, m_res->iconFormat,
        D2D1::RectF(startX, top, startX + ICON_SIZE, top + height), m_res->whiteBrush);

    // Bar background
    float barY = top + (height - BAR_HEIGHT) / 2.0f;
    float barX = startX + ICON_SIZE + 15.0f;
    m_res->grayBrush->SetOpacity(0.5f * contentAlpha);
    ctx->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + BAR_WIDTH, barY + BAR_HEIGHT), BAR_HEIGHT / 2.0f, BAR_HEIGHT / 2.0f),
        m_res->grayBrush);

    // Bar foreground
    if (m_volumeLevel > 0.0f) {
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + BAR_WIDTH * m_volumeLevel, barY + BAR_HEIGHT), BAR_HEIGHT / 2.0f, BAR_HEIGHT / 2.0f),
            m_res->whiteBrush);
    }
}

void VolumeComponent::DrawSecondary(float secLeft, float secTop, float secWidth, float secHeight, float secAlpha) {
    if (!m_res || secHeight <= 0.1f) return;
    auto* ctx = m_res->d2dContext;

    float secRight = secLeft + secWidth;
    float secBottom = secTop + secHeight;

    // 主岛背景由 RenderEngine 统一绘制，这里只画内容
    if (secHeight > 15.0f) {
        float contentAlpha = secAlpha;
        float iconSize = 18.0f;
        float barWidth = secWidth - iconSize - 40.0f;
        float barHeight = 5.0f;
        float startX = secLeft + (secWidth - (iconSize + 15.0f + barWidth)) / 2.0f;
        float barY = secTop + (secHeight - barHeight) / 2.0f;

        // Volume icon
        const wchar_t* volIcon = GetVolumeIcon(m_volumeLevel);
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextW(volIcon, 1, m_res->iconFormat,
            D2D1::RectF(startX, secTop, startX + iconSize, secBottom), m_res->whiteBrush);

        // Bar background
        float barX = startX + iconSize + 15.0f;
        m_res->grayBrush->SetOpacity(0.4f * contentAlpha);
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight), barHeight / 2.0f, barHeight / 2.0f),
            m_res->grayBrush);

        // Bar foreground
        if (m_volumeLevel > 0.0f) {
            m_res->whiteBrush->SetOpacity(contentAlpha);
            ctx->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barWidth * m_volumeLevel, barY + barHeight), barHeight / 2.0f, barHeight / 2.0f),
                m_res->whiteBrush);
        }
    }
}
