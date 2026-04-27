#include "LayoutController.h"
#include "Constants.h"
#include <algorithm>

LayoutController::LayoutController()
    : m_currentWidth(Constants::Size::COLLAPSED_WIDTH)
    , m_currentHeight(Constants::Size::COLLAPSED_HEIGHT)
    , m_currentAlpha(1.0f)
{
    // Spring parameters tuned for snappy expand/collapse:
    m_widthSpring.SetStiffness(470.0f);
    m_widthSpring.SetDamping(34.0f);
    m_heightSpring.SetStiffness(470.0f);
    m_heightSpring.SetDamping(34.0f);
    m_alphaSpring.SetStiffness(230.0f);  // Alpha fades slower
    m_alphaSpring.SetDamping(18.0f);
    
    // 副岛弹簧 [新增]
    m_secHeightSpring.SetStiffness(145.0f); // 降低劲度，让动画更慢、更优雅
    m_secHeightSpring.SetDamping(16.0f);    // 相应降低阻尼，保持自然的弹簧质感
    m_secAlphaSpring.SetStiffness(125.0f);
    m_secAlphaSpring.SetDamping(13.0f);

    m_widthSpring.SetTarget(Constants::Size::COLLAPSED_WIDTH);
    m_heightSpring.SetTarget(Constants::Size::COLLAPSED_HEIGHT);
    m_alphaSpring.SetTarget(1.0f);
    m_secHeightSpring.SetTarget(0.0f);
    m_secAlphaSpring.SetTarget(0.0f);
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

void LayoutController::SetSecondaryTarget(float h, float a) {
    m_targetSecHeight = h;
    m_targetSecAlpha = a;
    m_secHeightSpring.SetTarget(h);
    m_secAlphaSpring.SetTarget(a);
}

void LayoutController::SetSpringParams(float stiffness, float damping) {
    stiffness = (std::max)(10.0f, stiffness);
    damping = (std::max)(1.0f, damping);

    m_widthSpring.SetStiffness(stiffness);
    m_widthSpring.SetDamping(damping);
    m_heightSpring.SetStiffness(stiffness);
    m_heightSpring.SetDamping(damping);

    m_alphaSpring.SetStiffness(stiffness * 0.5f);
    m_alphaSpring.SetDamping((std::max)(1.0f, damping * 0.5f));
    m_secHeightSpring.SetStiffness(stiffness * 0.34f);
    m_secHeightSpring.SetDamping((std::max)(1.0f, damping * 0.48f));
    m_secAlphaSpring.SetStiffness(stiffness * 0.28f);
    m_secAlphaSpring.SetDamping((std::max)(1.0f, damping * 0.42f));
}

void LayoutController::StartAnimation() {
    m_isAnimating = true;
}

void LayoutController::UpdatePhysics() {
    // Update springs
    m_widthSpring.Update(0.016f);   // ~60fps delta
    m_heightSpring.Update(0.016f);
    m_alphaSpring.Update(0.016f);
    m_secHeightSpring.Update(0.016f); // [新增]
    m_secAlphaSpring.Update(0.016f);  // [新增]

    // Sample current values
    m_currentWidth = m_widthSpring.GetValue();
    m_currentHeight = m_heightSpring.GetValue();
    m_currentAlpha = m_alphaSpring.GetValue();
    m_currentSecHeight = m_secHeightSpring.GetValue(); // [新增]
    m_currentSecAlpha = m_secAlphaSpring.GetValue();   // [新增]

    // Check if settled
    if (m_widthSpring.IsSettled() && m_heightSpring.IsSettled() && m_alphaSpring.IsSettled() &&
        m_secHeightSpring.IsSettled() && m_secAlphaSpring.IsSettled()) {
        m_isAnimating = false;
    }
}

int LayoutController::HitTestPlaybackButtons(POINT pt, bool isExpanded, bool hasSession,
    float canvasWidth, float currentWidth, float currentHeight, float dpiScale) const
{
    if (!hasSession) return -1;

    float left = (canvasWidth - currentWidth) / 2.0f;
    float top = Constants::UI::TOP_MARGIN;
    if (!isExpanded) {
        const float buttonSize = 28.0f;
        const float spacing = Constants::UI::BUTTON_SPACING;
        const float buttonX = left + currentWidth - 104.0f;
        const float buttonY = top + (currentHeight - buttonSize) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const float x = buttonX + i * (buttonSize + spacing);
            RECT r = {
                (long)(x - 4.0f),
                (long)(buttonY - 4.0f),
                (long)(x + buttonSize + 4.0f),
                (long)(buttonY + buttonSize + 4.0f)
            };
            if (PtInRect(&r, pt)) {
                return i;
            }
        }
        return -1;
    }

    const float buttonSize = 36.0f;
    const float buttonGap = 22.0f;
    const float playSize = 40.0f;
    const float buttonGroupWidth = buttonSize * 4.0f + playSize + buttonGap * 4.0f;
    const float buttonX = left + (currentWidth - buttonGroupWidth) * 0.5f;
    const float buttonY = top + currentHeight - 48.0f;

    for (int i = 0; i < 5; ++i) {
        const bool isPlay = i == 2;
        const float currentSize = isPlay ? playSize : buttonSize;
        float x = buttonX + i * (buttonSize + buttonGap);
        if (i > 2) {
            x += 4.0f;
        }
        RECT r = {
            (long)(x - 4.0f),
            (long)(buttonY + (buttonSize - currentSize) * 0.5f - 4.0f),
            (long)(x + currentSize + 4.0f),
            (long)(buttonY + (buttonSize - currentSize) * 0.5f + currentSize + 4.0f)
        };
        if (PtInRect(&r, pt)) {
            return i;
        }
    }

    return -1;
}

int LayoutController::HitTestProgressBar(POINT pt, bool isExpanded, bool hasSession,
    float canvasWidth, float currentWidth, float currentHeight, float dpiScale) const
{
    if (!isExpanded || !hasSession) return -1;

    const float left = (canvasWidth - currentWidth) / 2.0f;
    const float top = Constants::UI::TOP_MARGIN;
    const float progressBarLeft = left + 66.0f;
    const float progressBarRight = left + currentWidth - 66.0f;
    const float progressBarY = top + currentHeight - 66.0f;
    const float progressBarHeight = 3.5f;

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
