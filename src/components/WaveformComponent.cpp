#include "components/WaveformComponent.h"
#include <cstdlib>

void WaveformComponent::Update(float deltaTime) {
    (void)deltaTime;
    float minH = 5.0f;
    float maxAdd = m_audioLevel * 50.0f;
    for (int i = 0; i < 3; ++i) {
        float target = minH + (rand() / float(RAND_MAX)) * maxAdd;
        float maxAllowed = m_islandHeight - 12.0f;
        if (target > maxAllowed) target = maxAllowed;
        m_targetHeight[i] = target;
    }

    const float smooth = 0.5f;
    for (int i = 0; i < 3; ++i) {
        m_currentHeight[i] += (m_targetHeight[i] - m_currentHeight[i]) * smooth;
    }
}

void WaveformComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || m_audioLevel <= 0.0f) return;

    auto* ctx = m_res->d2dContext;
    auto* tb = m_res->themeBrush;
    tb->SetOpacity(contentAlpha);

    float height = rect.bottom - rect.top;
    float baseBottom = (height >= 60.0f) ? (rect.top + 75.0f) : (rect.bottom - 10.0f);
    float right = (height >= 60.0f) ? (rect.right - 40.0f) : (rect.right - 20.0f);
    float h1 = m_currentHeight[0] * 0.5f;
    float h2 = m_currentHeight[1] * 0.5f;
    float h3 = m_currentHeight[2] * 0.5f;

    ctx->FillRectangle(D2D1::RectF(right - 20.0f, baseBottom - h1, right - 16.0f, baseBottom), tb);
    ctx->FillRectangle(D2D1::RectF(right - 12.0f, baseBottom - h2, right - 8.0f, baseBottom), tb);
    ctx->FillRectangle(D2D1::RectF(right - 4.0f, baseBottom - h3, right, baseBottom), tb);
}
