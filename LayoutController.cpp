#include "LayoutController.h"
#include "Constants.h"

LayoutController::LayoutController()
    : m_currentWidth(Constants::Size::COLLAPSED_WIDTH)
    , m_currentHeight(Constants::Size::COLLAPSED_HEIGHT)
    , m_currentAlpha(1.0f)
{
    // Spring parameters tuned for snappy expand/collapse:
    // stiffness=400: fast response (natural freq ~20 rad/s)
    // damping=30: critically damped (zeta~1.07), minimal overshoot
    // Formula: zeta = c / (2*sqrt(k*m)) = 30/(2*sqrt(400)) = 30/40 = 0.75
    // At zeta=0.75, settles in ~3/frequency = ~150ms, no excessive oscillation
    m_widthSpring.SetStiffness(400.0f);
    m_widthSpring.SetDamping(30.0f);
    m_heightSpring.SetStiffness(400.0f);
    m_heightSpring.SetDamping(30.0f);
    m_alphaSpring.SetStiffness(200.0f);  // Alpha fades slower
    m_alphaSpring.SetDamping(15.0f);
    m_widthSpring.SetTarget(Constants::Size::COLLAPSED_WIDTH);
    m_heightSpring.SetTarget(Constants::Size::COLLAPSED_HEIGHT);
    m_alphaSpring.SetTarget(1.0f);
}

void LayoutController::SetTargetSize(float w, float h) {
    m_targetWidth = w;
    m_targetHeight = h;
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

int LayoutController::HitTestFileItem(POINT pt, const std::vector<std::wstring>& files, float canvasWidth,
    float currentWidth, float currentHeight, float dpiScale) const
{
    float left = (canvasWidth - currentWidth) / 2.0f;
    float top = 10.0f;
    float PADDING = 12.0f;
    float ITEM_HEIGHT = 40.0f;
    float GAP = 4.0f;

    float currentY = top + PADDING + 5.0f; // 匹配 RenderFileList 的偏移
    float contentWidth = currentWidth - (PADDING * 2);

    for (size_t i = 0; i < files.size(); ++i) {
        RECT itemRect = { 
            (long)(left + PADDING), 
            (long)currentY, 
            (long)(left + PADDING + contentWidth), 
            (long)(currentY + ITEM_HEIGHT) 
        };

        if (PtInRect(&itemRect, pt)) {
            return (int)i;
        }

        currentY += ITEM_HEIGHT + GAP;
        if (currentY > top + currentHeight + 10.0f) break;
    }

    return -1;
}

bool LayoutController::HitTestFileDelete(POINT pt, int fileIndex, float canvasWidth,
    float currentWidth, float currentHeight, float dpiScale) const
{
    if (fileIndex < 0) return false;

    float left = (canvasWidth - currentWidth) / 2.0f;
    float top = 10.0f;
    float PADDING = 12.0f;
    float ITEM_HEIGHT = 40.0f;
    float GAP = 4.0f;
    float DELETE_BTN_SIZE = 22.0f;

    float itemY = top + PADDING + 5.0f + (float)fileIndex * (ITEM_HEIGHT + GAP);
    float contentWidth = currentWidth - (PADDING * 2);

    float delX = left + PADDING + contentWidth - DELETE_BTN_SIZE - 8.0f;
    float delY = itemY + (ITEM_HEIGHT - DELETE_BTN_SIZE) / 2.0f;

    RECT delRect = { 
        (long)delX, 
        (long)delY, 
        (long)(delX + DELETE_BTN_SIZE), 
        (long)(delY + DELETE_BTN_SIZE) 
    };

    // 适当扩大点击判定范围
    InflateRect(&delRect, 8, 8);

    return PtInRect(&delRect, pt);
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
