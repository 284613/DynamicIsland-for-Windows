#pragma once
#include "IIslandComponent.h"

#include <algorithm>
#include <array>

enum class WaveformDisplayStyle {
    Compact,
    Expanded
};

class WaveformComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return (std::max)({ m_bandLevels[0], m_bandLevels[1], m_bandLevels[2] }) > 0.0f; }
    bool NeedsRender() const override { return IsActive() || (m_styleBlend > 0.01f && m_styleBlend < 0.99f); }

    void SetBandLevels(const std::array<float, 3>& levels) { m_bandLevels = levels; }
    void SetIslandHeight(float h)   { m_islandHeight = h; }
    void SetDisplayStyle(WaveformDisplayStyle style) { m_targetStyle = style; }

private:
    std::array<float, 3> NormalizedBands() const;
    void DrawCompact(const D2D1_RECT_F& rect, float contentAlpha);
    void DrawExpanded(const D2D1_RECT_F& rect, float contentAlpha);

    SharedResources* m_res = nullptr;
    ComPtr<ID2D1SolidColorBrush> m_waveBrush;
    std::array<float, 3> m_bandLevels{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> m_normalizedBands{ 0.0f, 0.0f, 0.0f };
    WaveformDisplayStyle m_targetStyle = WaveformDisplayStyle::Compact;
    float m_styleBlend = 0.0f;
    float m_islandHeight = 40.0f;
    float m_phase = 0.0f;
    float m_energyPeak = 0.12f;
    float m_lastEnergy = 0.0f;
    float m_beatPulse = 0.0f;

    float m_currentHeight[3] = { 10.f, 10.f, 10.f };
    float m_targetHeight[3]  = { 10.f, 10.f, 10.f };
    std::array<float, 10> m_compactHeights{};
    std::array<float, 10> m_compactTargets{};
    std::array<float, 24> m_expandedHeights{};
    std::array<float, 24> m_expandedTargets{};
};
