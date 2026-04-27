#include "components/WaveformComponent.h"

#include <algorithm>
#include <cmath>

std::array<float, 3> WaveformComponent::NormalizedBands() const {
    return m_normalizedBands;
}

void WaveformComponent::OnAttach(SharedResources* res) {
    m_res = res;
    if (m_res && m_res->d2dContext) {
        m_res->d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
            &m_waveBrush);
    }
}

void WaveformComponent::Update(float deltaTime) {
    const float dt = (std::max)(0.001f, (std::min)(0.05f, deltaTime > 0.0f ? deltaTime : 0.016f));
    const float targetBlend = m_targetStyle == WaveformDisplayStyle::Expanded ? 1.0f : 0.0f;
    m_styleBlend += (targetBlend - m_styleBlend) * (std::min)(1.0f, dt * 9.0f);
    if (std::fabs(targetBlend - m_styleBlend) < 0.01f) {
        m_styleBlend = targetBlend;
    }

    const float maxAllowed = (std::max)(14.0f, m_islandHeight - 8.0f);
    const float rawEnergy = (m_bandLevels[0] * 0.42f) + (m_bandLevels[1] * 0.38f) + (m_bandLevels[2] * 0.20f);
    const float peakAttack = (std::min)(1.0f, dt * 10.0f);
    const float peakDecay = std::pow(0.68f, dt);
    if (rawEnergy > m_energyPeak) {
        m_energyPeak += (rawEnergy - m_energyPeak) * peakAttack;
    } else {
        m_energyPeak = (std::max)(0.08f, m_energyPeak * peakDecay);
    }

    const float gain = 0.62f / (std::max)(0.08f, m_energyPeak);
    for (size_t i = 0; i < m_bandLevels.size(); ++i) {
        const float band = (std::max)(0.0f, (std::min)(1.0f, m_bandLevels[i]));
        const float normalized = std::pow((std::min)(1.0f, band * gain + 0.035f), 0.70f);
        m_normalizedBands[i] += (normalized - m_normalizedBands[i]) * (std::min)(1.0f, dt * 14.0f);
    }

    const float transient = (std::max)(0.0f, rawEnergy - m_lastEnergy);
    m_lastEnergy += (rawEnergy - m_lastEnergy) * (std::min)(1.0f, dt * 9.0f);
    m_beatPulse = (std::max)(m_beatPulse * std::pow(0.22f, dt), (std::min)(1.0f, transient * gain * 2.6f));
    const float rhythmEnergy = (std::max)({ m_normalizedBands[0], m_normalizedBands[1], m_normalizedBands[2], m_beatPulse });
    m_phase += dt * (5.2f + rhythmEnergy * 9.5f);

    const float pulses[3] = {
        0.92f + 0.08f * std::sinf(m_phase),
        0.90f + 0.10f * std::sinf(m_phase * 1.23f + 1.35f),
        0.88f + 0.12f * std::sinf(m_phase * 1.67f + 2.45f),
    };
    const float weights[3] = { 1.25f, 1.45f, 1.30f };

    for (int i = 0; i < 3; ++i) {
        const float bandLevel = (std::max)(0.0f, (std::min)(1.0f, m_normalizedBands[i] + m_beatPulse * 0.22f));
        float target = 6.0f + bandLevel * maxAllowed * weights[i] * pulses[i];
        target = (std::min)(target, maxAllowed);
        m_targetHeight[i] = target;
    }

    const float smoothing = (std::min)(1.0f, dt * 18.0f);
    for (int i = 0; i < 3; ++i) {
        m_currentHeight[i] += (m_targetHeight[i] - m_currentHeight[i]) * smoothing;
    }

    const float compactMax = (std::max)(18.0f, (std::min)(28.0f, m_islandHeight * 0.42f));
    for (size_t i = 0; i < m_compactTargets.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(m_compactTargets.size() - 1);
        const float lowShape = 1.0f - t * 0.72f;
        const float midShape = 1.0f - std::fabs(t - 0.48f) * 1.55f;
        const float highShape = 0.28f + t * 1.08f;
        const float bandMix =
            m_normalizedBands[0] * lowShape * 1.45f +
            m_normalizedBands[1] * (std::max)(0.0f, midShape) * 1.70f +
            m_normalizedBands[2] * highShape * 1.05f;
        const float pulse = 0.58f + 0.30f * std::sinf(m_phase * (1.75f + t * 0.70f) + t * 6.40f) + m_beatPulse * 0.34f;
        m_compactTargets[i] = 5.0f + std::pow((std::min)(1.0f, bandMix + m_beatPulse * 0.22f), 0.54f) * compactMax * pulse;
    }

    const float compactSmoothing = (std::min)(1.0f, dt * 20.0f);
    for (size_t i = 0; i < m_compactHeights.size(); ++i) {
        m_compactHeights[i] += (m_compactTargets[i] - m_compactHeights[i]) * compactSmoothing;
    }

    const float expandedMax = (std::max)(32.0f, (std::min)(46.0f, m_islandHeight * 0.34f));
    for (size_t i = 0; i < m_expandedTargets.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(m_expandedTargets.size() - 1);
        const float lowShape = 1.0f - t * 0.70f;
        const float midShape = (std::max)(0.0f, 1.0f - std::fabs(t - 0.48f) * 1.45f);
        const float highShape = 0.32f + t * 1.05f;
        const float bandMix =
            m_normalizedBands[0] * lowShape * 1.35f +
            m_normalizedBands[1] * midShape * 1.62f +
            m_normalizedBands[2] * highShape * 1.12f;
        const float pulse = 0.54f + 0.36f * std::sinf(m_phase * (1.70f + t * 0.76f) + t * 6.10f) + m_beatPulse * 0.40f;
        m_expandedTargets[i] = 7.0f + std::pow((std::min)(1.0f, bandMix + m_beatPulse * 0.26f), 0.52f) * expandedMax * pulse;
    }

    const float expandedSmoothing = (std::min)(1.0f, dt * 22.0f);
    for (size_t i = 0; i < m_expandedHeights.size(); ++i) {
        m_expandedHeights[i] += (m_expandedTargets[i] - m_expandedHeights[i]) * expandedSmoothing;
    }
}

void WaveformComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_waveBrush || !IsActive()) {
        return;
    }

    const float expandedAlpha = m_styleBlend;
    const float compactAlpha = 1.0f - m_styleBlend;
    if (compactAlpha > 0.01f) {
        DrawCompact(rect, contentAlpha * compactAlpha);
    }
    if (expandedAlpha > 0.01f) {
        DrawExpanded(rect, contentAlpha * expandedAlpha);
    }
}

void WaveformComponent::DrawCompact(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    m_waveBrush->SetOpacity(0.78f * contentAlpha);

    const float height = rect.bottom - rect.top;
    const float centerY = rect.top + height * 0.54f;
    const float right = rect.right - 14.0f;
    const float barWidth = 2.2f;
    const float gap = 1.6f;
    const float maxBarHeight = (std::min)(22.0f, (std::max)(14.0f, height - 34.0f));
    const float totalWidth = barWidth * static_cast<float>(m_compactHeights.size()) + gap * static_cast<float>(m_compactHeights.size() - 1);
    const float left = right - totalWidth;
    for (size_t i = 0; i < m_compactHeights.size(); ++i) {
        const float x = left + static_cast<float>(i) * (barWidth + gap);
        const float h = (std::min)(maxBarHeight,
            (std::max)(4.0f, m_compactHeights[i] * (0.58f + 0.06f * static_cast<float>(i % 4))));
        const D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(
            D2D1::RectF(x, centerY - h * 0.52f, x + barWidth, centerY + h * 0.52f),
            barWidth * 0.5f,
            barWidth * 0.5f);
        ctx->FillRoundedRectangle(&bar, m_waveBrush.Get());
    }
}

void WaveformComponent::DrawExpanded(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    const float right = rect.right - 24.0f;
    const float left = (std::max)(rect.left + 210.0f, right - 126.0f);
    const float width = right - left;
    if (width <= 40.0f) {
        return;
    }

    const float laneTop = rect.top + 18.0f;
    const float laneBottom = rect.top + 62.0f;
    if (laneBottom <= laneTop + 18.0f) {
        return;
    }
    const float centerY = (laneTop + laneBottom) * 0.5f;
    const float maxBarHeight = (std::min)(38.0f, (laneBottom - laneTop) * 0.86f);
    const float barWidth = 4.0f;
    constexpr size_t visibleBars = 16;
    const float gap = (std::max)(3.0f, (width - barWidth * static_cast<float>(visibleBars)) /
        static_cast<float>(visibleBars - 1));

    m_waveBrush->SetOpacity(0.86f * contentAlpha);
    for (size_t i = 0; i < visibleBars; ++i) {
        const float x = left + static_cast<float>(i) * (barWidth + gap);
        const size_t sampleIndex = (i * (m_expandedHeights.size() - 1)) / (visibleBars - 1);
        const float drawPulse = 0.90f + 0.16f * std::sinf(m_phase * (1.35f + static_cast<float>(i) * 0.035f) + static_cast<float>(i) * 0.82f);
        const float h = (std::min)(maxBarHeight,
            (std::max)(8.0f, m_expandedHeights[sampleIndex] * drawPulse * (0.84f + 0.07f * static_cast<float>(i % 4))));
        const D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(
            D2D1::RectF(x, centerY - h * 0.50f, x + barWidth, centerY + h * 0.50f),
            barWidth * 0.5f,
            barWidth * 0.5f);
        ctx->FillRoundedRectangle(&bar, m_waveBrush.Get());
    }
}
