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
    bool LoadSnapshot();
    bool SaveSnapshot() const;
    void ClearSnapshot();

    void SetOnFinished(std::function<void()> callback) { m_onFinished = std::move(callback); }

private:
    enum class State { Idle, Setting, Running, Paused, Finished };
    enum class ButtonId {
        None = 0,
        CustomMinus,
        CustomPlus,
        StartPause,
        Stop,
        Collapse,
        Dial,
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
    void DrawDial(float cx, float cy, float radius, float alpha, bool interactive);
    void DrawHand(float cx, float cy, float radius, float alpha);
    void DrawButton(const ButtonRect& button, const wchar_t* icon, bool hovered, float alpha,
        float radius = 8.0f, IDWriteTextFormat* format = nullptr);
    void DrawActionButton(const ButtonRect& button, ButtonId id, bool primary, bool hovered, float alpha);
    void DrawTextCentered(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        ID2D1SolidColorBrush* brush, float alpha);
    void DrawPlayIcon(const D2D1_RECT_F& rect, float alpha);
    void DrawPauseIcon(const D2D1_RECT_F& rect, float alpha);
    void DrawCancelIcon(const D2D1_RECT_F& rect, float alpha);
    void DrawCollapseIcon(const D2D1_RECT_F& rect, float alpha);
    ButtonId HitTest(float x, float y) const;
    void RebuildExpandedLayout(const D2D1_RECT_F& rect);
    void RebuildCompactLayout(const D2D1_RECT_F& rect);
    void AdjustCustomMinutes(int deltaMinutes);
    void SetSelectedMinutes(int minutes, bool animateHand);
    float MinutesToAngle(int minutes) const;
    int PointToMinutes(float x, float y) const;
    int GetRemainingMinuteTicks() const;
    bool IsDialInteractive() const;
    void SyncStateFromTimer();
    void StopSession(bool finished);
    void CollapseToCompact();
    std::wstring GetExpandedCenterText() const;
    std::wstring GetCompactText() const;
    std::wstring GetStatusText() const;
    std::wstring GetDurationText() const;
    std::wstring SnapshotStateLabel() const;

    SharedResources* m_res = nullptr;
    ComPtr<ID2D1SolidColorBrush> m_pulseBrush;
    ComPtr<IDWriteTextFormat> m_timerFormat;
    ComPtr<IDWriteTextFormat> m_captionFormat;
    ComPtr<IDWriteTextFormat> m_actionFormat;
    PomodoroTimer m_timer;
    State m_state = State::Idle;
    bool m_expanded = false;
    int m_selectedMinutes = 25;
    int m_customMinutes = 30;
    int m_hoveredButton = -1;
    float m_handAngleCurrent = 0.0f;
    float m_handAngleTarget = 0.0f;
    bool m_handAnimating = false;
    float m_snapshotElapsed = 0.0f;
    D2D1_RECT_F m_lastRect = D2D1::RectF(0, 0, 0, 0);
    std::vector<ButtonRect> m_buttons;
    std::function<void()> m_onFinished;
};
