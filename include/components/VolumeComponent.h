// VolumeComponent.h
#pragma once
#include "IIslandComponent.h"

class VolumeComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_isActive; }

    void SetActive(bool active) { m_isActive = active; }
    void SetVolumeLevel(float level) { m_volumeLevel = level; }

private:
    const wchar_t* GetVolumeIcon(float level);

    SharedResources* m_res = nullptr;
    bool  m_isActive     = false;
    float m_volumeLevel  = 0.0f;

    static constexpr float ICON_SIZE  = 20.0f;
    static constexpr float BAR_WIDTH  = 120.0f;
    static constexpr float BAR_HEIGHT = 6.0f;
};
