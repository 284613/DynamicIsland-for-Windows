#pragma once

#include "components/IIslandComponent.h"

#include <string>

enum class FaceIdState {
    Hidden,
    Scanning,
    Success,
    Failed,
};

class FaceIdComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_state != FaceIdState::Hidden; }
    bool NeedsRender() const override { return m_state != FaceIdState::Hidden; }

    void SetState(FaceIdState state, std::wstring text = L"");

private:
    void DrawScanning(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawSuccess(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawFailed(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawArc(float cx, float cy, float radius, float startAngle, float sweepAngle,
        ID2D1Brush* brush, float strokeWidth);
    void DrawCenteredText(const std::wstring& text, const D2D1_RECT_F& rect, float alpha);

    SharedResources* m_res = nullptr;
    FaceIdState m_state = FaceIdState::Hidden;
    std::wstring m_text;
    ULONGLONG m_startedMs = 0;
    float m_phase = 0.0f;

    ComPtr<ID2D1SolidColorBrush> m_indigoBrush;
    ComPtr<ID2D1SolidColorBrush> m_greenBrush;
    ComPtr<ID2D1SolidColorBrush> m_redBrush;
    ComPtr<ID2D1SolidColorBrush> m_softBrush;
    ComPtr<IDWriteTextFormat> m_labelFormat;
};
