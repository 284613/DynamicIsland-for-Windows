#include "LayoutController.h"
#include "Constants.h"

LayoutController::LayoutController()
    : m_currentWidth(Constants::Size::COLLAPSED_WIDTH)
    , m_currentHeight(Constants::Size::COLLAPSED_HEIGHT)
    , m_currentAlpha(1.0f)
{
    m_widthSpring.SetTarget(Constants::Size::COLLAPSED_WIDTH);
    m_heightSpring.SetTarget(Constants::Size::COLLAPSED_HEIGHT);
    m_alphaSpring.SetTarget(1.0f);
}

void LayoutController::SetTargetSize(float w, float h) {
    m_widthSpring.SetTarget(w);
    m_heightSpring.SetTarget(h);
}

void LayoutController::SetTargetAlpha(float a) {
    m_targetAlpha = a;
    m_alphaSpring.SetTarget(a);
}

void LayoutController::StartAnimation() {
    m_isAnimating = true;
}

void LayoutController::UpdatePhysics() {
    // Update springs
    m_widthSpring.Update(0.016f);   // ~60fps delta
    m_heightSpring.Update(0.016f);
    m_alphaSpring.Update(0.016f);

    // Sample current values
    m_currentWidth = m_widthSpring.GetValue();
    m_currentHeight = m_heightSpring.GetValue();
    m_currentAlpha = m_alphaSpring.GetValue();

    // Check if settled
    if (m_widthSpring.IsSettled() && m_heightSpring.IsSettled() && m_alphaSpring.IsSettled()) {
        m_isAnimating = false;
    }
}

int LayoutController::HitTestPlaybackButtons(POINT pt, bool isExpanded, bool hasSession,
    float canvasWidth, float currentWidth, float currentHeight, float dpiScale) const
{
    if (!isExpanded || !hasSession) return -1;

    float left = (canvasWidth - currentWidth) / 2.0f;
    float top = 10.0f;
    float right = left + currentWidth;
    float artSize = 60.0f;
    float artLeft = left + 20.0f;
    float artTop = top + 30.0f;
    float textLeft = artLeft + artSize + 15.0f;
    float textRight = right - 20.0f;
    float titleMaxWidth = textRight - textLeft;
    float buttonSize = Constants::UI::BUTTON_SIZE;
    float spacing = Constants::UI::BUTTON_SPACING;
    float buttonGroupWidth = buttonSize * 3 + spacing * 2;
    float artistBottom = artTop + 60.0f;
    float progressBarY = artistBottom + 20.0f;
    float progressBarHeight = 6.0f;
    float buttonY = progressBarY + progressBarHeight + 10.0f;
    float buttonX = textLeft + (titleMaxWidth - buttonGroupWidth) / 2.0f - 45.0f;
    if (buttonX < textLeft) buttonX = textLeft;

    RECT prevRect = { (long)buttonX, (long)buttonY, (long)(buttonX + buttonSize), (long)(buttonY + buttonSize) };
    RECT playRect = { (long)(buttonX + buttonSize + spacing), (long)buttonY, (long)(buttonX + 2 * buttonSize + spacing), (long)(buttonY + buttonSize) };
    RECT nextRect = { (long)(buttonX + 2 * (buttonSize + spacing)), (long)buttonY, (long)(buttonX + 3 * buttonSize + 2 * spacing), (long)(buttonY + buttonSize) };

    if (PtInRect(&prevRect, pt)) return 0;
    if (PtInRect(&playRect, pt)) return 1;
    if (PtInRect(&nextRect, pt)) return 2;

    return -1;
}

int LayoutController::HitTestProgressBar(POINT pt, bool isExpanded, bool hasSession,
    float canvasWidth, float currentWidth, float currentHeight, float dpiScale) const
{
    if (!isExpanded || !hasSession) return -1;

    float left = (canvasWidth - currentWidth) / 2.0f;
    float top = 10.0f;
    float right = left + currentWidth;
    float artSize = 60.0f;
    float artLeft = left + 20.0f;
    float artTop = top + 30.0f;
    float textLeft = artLeft + artSize + 15.0f;
    float titleMaxWidth = (right - 20.0f) - textLeft;
    float progressBarLeft = textLeft - 80.0f;
    float progressBarRight = textLeft + titleMaxWidth;
    float artistBottom = artTop + 60.0f;
    float progressBarY = artistBottom + 20.0f;
    float progressBarHeight = 6.0f;

    // 扩大点击区域方便操作
    float hitTop = progressBarY - 10.0f;
    float hitBottom = progressBarY + progressBarHeight + 10.0f;

    if (pt.x >= progressBarLeft && pt.x <= progressBarRight && pt.y >= hitTop && pt.y <= hitBottom) {
        return 1;
    }

    return -1;
}

LayoutController::LayoutRects LayoutController::ComputeLayout(float currentWidth, float currentHeight, float dpiScale) const {
    LayoutRects r{};
    r.albumArtLeft = 20.0f;
    r.albumArtTop = 30.0f;
    r.albumArtSize = Constants::UI::ALBUM_ART_SIZE;
    r.buttonAreaLeft = r.albumArtLeft + r.albumArtSize + 15.0f;
    r.progressBarLeft = r.buttonAreaLeft - 80.0f;
    r.progressBarTop = r.albumArtTop + 60.0f + 20.0f;
    r.progressBarWidth = 200.0f;
    return r;
}
