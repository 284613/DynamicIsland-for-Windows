#pragma once
#include "IIslandComponent.h"

class WaveformComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_audioLevel > 0.0f; }
    bool NeedsRender() const override { return m_audioLevel > 0.0f; }

    void SetAudioLevel(float level) { m_audioLevel = level; }
    void SetIslandHeight(float h)   { m_islandHeight = h; }

private:
    SharedResources* m_res = nullptr;
    ComPtr<ID2D1SolidColorBrush> m_waveBrush;
    float m_audioLevel   = 0.0f;
    float m_islandHeight = 40.0f;

    float m_currentHeight[3] = { 10.f, 10.f, 10.f };
    float m_targetHeight[3]  = { 10.f, 10.f, 10.f };
};
