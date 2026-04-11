#include "components/ClockComponent.h"
#include <dwrite.h>

ClockComponent::ClockComponent() = default;

void ClockComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void ClockComponent::SetTimeData(bool show, const std::wstring& text) {
    m_showTime = show;
    m_timeText = text;
}

void ClockComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !IsActive()) {
        m_lastTextRect = D2D1::RectF(0, 0, 0, 0);
        return;
    }

    auto* ctx = m_res->d2dContext;
    if (!ctx || !m_res->dwriteFactory || !m_res->titleFormat) return;

    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;

    // Create text layout
    ComPtr<IDWriteTextLayout> timeLayout;
    HRESULT hr = m_res->dwriteFactory->CreateTextLayout(
        m_timeText.c_str(),
        (UINT32)m_timeText.length(),
        m_res->titleFormat,
        200.0f,
        100.0f,
        &timeLayout);
    if (FAILED(hr) || !timeLayout) return;

    DWRITE_TEXT_METRICS metrics;
    timeLayout->GetMetrics(&metrics);

    float textX = rect.left + (width - metrics.width) / 2.0f;
    float textY = rect.top + (height - metrics.height) / 2.0f;
    m_lastTextRect = D2D1::RectF(textX - 8.0f, rect.top, textX + metrics.width + 8.0f, rect.bottom);

    if (m_res->whiteBrush) {
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textX, textY), timeLayout.Get(), m_res->whiteBrush);
    }
}

bool ClockComponent::OnMouseClick(float x, float y) {
    if (!IsActive()) {
        return false;
    }

    if (x < m_lastTextRect.left || x > m_lastTextRect.right || y < m_lastTextRect.top || y > m_lastTextRect.bottom) {
        return false;
    }

    if (m_onClick) {
        m_onClick();
    }
    return true;
}
