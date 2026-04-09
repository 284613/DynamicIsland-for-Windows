// ClockComponent.h
#pragma once
#include "IIslandComponent.h"
#include <wrl/client.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <string>

class ClockComponent : public IIslandComponent {
public:
    ClockComponent();
    virtual ~ClockComponent() = default;

    void OnAttach(SharedResources* res) override;
    void OnResize(float dpi, int width, int height) override {}
    void Update(float deltaTime) override {}
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_showTime && !m_timeText.empty(); }
    bool NeedsRender() const override { return false; }

    void SetTimeData(bool show, const std::wstring& text);

private:
    SharedResources* m_res = nullptr;
    bool m_showTime = false;
    std::wstring m_timeText;
};
