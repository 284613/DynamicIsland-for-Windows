#pragma once
#include "IIslandComponent.h"
#include "PomodoroTimer.h"
#include <functional>
#include <vector>

class PomodoroComponent : public IIslandComponent {
public:
    PomodoroComponent();

    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override;
    bool NeedsRender() const override;
    bool OnMouseClick(float x, float y) override;
    bool OnMouseMove(float x, float y) override;

    void SetExpanded(bool expanded);
    bool IsExpanded() const { return m_expanded; }
    bool IsRunning() const { return m_state == State::Running; }
    bool IsPaused() const { return m_state == State::Paused; }
    int GetRemainingSeconds() const;
    bool HasActiveSession() const;

    void SetOnFinished(std::function<void()> callback) { m_onFinished = std::move(callback); }

private:
    enum class State { Idle, Setting, Running, Paused, Finished };
    enum class ButtonId {
        None = 0,
        Preset25,
        Preset15,
        Preset5,
        CustomMinus,
        CustomPlus,
        StartPause,
        Stop,
        Collapse,
        CompactPause,
        CompactStop
    };

    struct ButtonRect {
        D2D1_RECT_F rect{};
        ButtonId id = ButtonId::None;
    };

    static bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y);
    static D2D1_RECT_F MakeRect(float left, float top, float width, float height);

    void DrawExpanded(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawCompact(const D2D1_RECT_F& rect, float alpha, ULONGLONG now);
    void DrawRing(float cx, float cy, float radius, float progress, float alpha);
    void DrawButton(const ButtonRect& button, const wchar_t* icon, bool hovered, float alpha,
        float radius = 8.0f, IDWriteTextFormat* format = nullptr);
    void DrawLabelButton(const ButtonRect& button, const std::wstring& text, bool selected, bool hovered, float alpha);
    void DrawTextCentered(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        ID2D1SolidColorBrush* brush, float alpha);
    ButtonId HitTest(float x, float y) const;
    void RebuildExpandedLayout(const D2D1_RECT_F& rect);
    void RebuildCompactLayout(const D2D1_RECT_F& rect);
    void ApplyPresetMinutes(int minutes);
    void AdjustCustomMinutes(int deltaMinutes);
    void SyncStateFromTimer();
    void StopSession(bool finished);
    void CollapseToCompact();
    std::wstring GetExpandedCenterText() const;
    std::wstring GetCompactText() const;

    SharedResources* m_res = nullptr;
    PomodoroTimer m_timer;
    State m_state = State::Idle;
    bool m_expanded = false;
    int m_selectedMinutes = 25;
    int m_customMinutes = 30;
    int m_hoveredButton = -1;
    D2D1_RECT_F m_lastRect = D2D1::RectF(0, 0, 0, 0);
    std::vector<ButtonRect> m_buttons;
    std::function<void()> m_onFinished;
};
