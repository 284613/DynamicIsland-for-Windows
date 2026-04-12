#include "components/PomodoroComponent.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
constexpr float kTomatoR = 0.91f;
constexpr float kTomatoG = 0.30f;
constexpr float kTomatoB = 0.24f;
constexpr float kPauseR = 0.95f;
constexpr float kPauseG = 0.61f;
constexpr float kPauseB = 0.07f;
constexpr float kDialStartAngle = -1.5707963f;
constexpr float kDialStep = 6.2831853f / 12.0f;
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;
constexpr float kDialCenterYOffset = 98.0f;

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

std::wstring FormatMinutes(int minutes) {
    return std::to_wstring(minutes) + L" min";
}

D2D1_COLOR_F AccentColor(float alpha = 1.0f) {
    return D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, alpha);
}

D2D1_COLOR_F PauseColor(float alpha = 1.0f) {
    return D2D1::ColorF(kPauseR, kPauseG, kPauseB, alpha);
}
}

PomodoroComponent::PomodoroComponent() = default;

void PomodoroComponent::OnAttach(SharedResources* res) {
    m_res = res;
    if (m_res && m_res->d2dContext) {
        m_res->d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, 1.0f),
            &m_pulseBrush);
    }
    if (m_res && m_res->dwriteFactory) {
        m_res->dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            30.0f, L"zh-cn", &m_timerFormat);
        m_res->dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f, L"zh-cn", &m_captionFormat);
        m_res->dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"zh-cn", &m_actionFormat);
    }
    m_handAngleCurrent = MinutesToAngle(m_selectedMinutes);
    m_handAngleTarget = m_handAngleCurrent;
    m_timer.SetDuration(m_selectedMinutes);
    m_timer.SetOnFinishCallback([this]() {
        m_state = State::Finished;
        m_expanded = false;
        if (m_onFinished) {
            m_onFinished();
        }
        StopSession(true);
    });
}

bool PomodoroComponent::IsActive() const {
    return m_expanded || HasActiveSession();
}

bool PomodoroComponent::NeedsRender() const {
    return IsRunning() || m_handAnimating;
}

void PomodoroComponent::SetExpanded(bool expanded) {
    m_expanded = expanded;
    if (expanded) {
        if (m_state == State::Idle || m_state == State::Finished) {
            m_state = State::Setting;
        }
        return;
    }

    if (m_state == State::Setting || m_state == State::Finished) {
        m_state = State::Idle;
    }
}

int PomodoroComponent::GetRemainingSeconds() const {
    return m_timer.GetRemainingSeconds();
}

bool PomodoroComponent::HasActiveSession() const {
    return m_state == State::Running || m_state == State::Paused;
}

void PomodoroComponent::Update(float deltaTime) {
    if (m_handAnimating) {
        const float speed = (std::min)(1.0f, deltaTime * 10.0f);
        m_handAngleCurrent += (m_handAngleTarget - m_handAngleCurrent) * speed;
        if (std::abs(m_handAngleCurrent - m_handAngleTarget) < 0.01f) {
            m_handAngleCurrent = m_handAngleTarget;
            m_handAnimating = false;
        }
    }
    if (!HasActiveSession()) {
        return;
    }
    m_timer.Update(deltaTime);
    SyncStateFromTimer();
}

void PomodoroComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res || !m_res->d2dContext || contentAlpha <= 0.01f || !IsActive()) {
        return;
    }

    m_lastRect = rect;
    if (m_expanded) {
        RebuildExpandedLayout(rect);
        DrawExpanded(rect, contentAlpha, currentTimeMs);
        return;
    }

    RebuildCompactLayout(rect);
    DrawCompact(rect, contentAlpha, currentTimeMs);
}

bool PomodoroComponent::OnMouseMove(float x, float y) {
    int previous = m_hoveredButton;
    ButtonId hit = HitTest(x, y);
    m_hoveredButton = (hit == ButtonId::None) ? -1 : static_cast<int>(hit);
    return previous != m_hoveredButton;
}

bool PomodoroComponent::OnMouseClick(float x, float y) {
    if (!IsActive()) {
        return false;
    }

    ButtonId hit = HitTest(x, y);
    if (!m_expanded) {
        if (hit == ButtonId::CompactPause) {
            if (m_state == State::Running) {
                m_timer.Pause();
                m_state = State::Paused;
            } else if (m_state == State::Paused) {
                m_timer.Start();
                m_state = State::Running;
            }
            return true;
        }
        if (hit == ButtonId::CompactStop) {
            StopSession(false);
            return true;
        }

        m_expanded = true;
        if (m_state == State::Idle || m_state == State::Finished) {
            m_state = State::Setting;
        }
        return true;
    }

    switch (hit) {
    case ButtonId::Dial:
        if (IsDialInteractive()) {
            const int minutes = PointToMinutes(x, y);
            SetSelectedMinutes(minutes, true);
        }
        return true;
    case ButtonId::CustomMinus:
        AdjustCustomMinutes(-5);
        return true;
    case ButtonId::CustomPlus:
        AdjustCustomMinutes(5);
        return true;
    case ButtonId::StartPause:
        if (m_state == State::Running) {
            m_timer.Pause();
            m_state = State::Paused;
        } else {
            if (m_state == State::Setting || m_state == State::Idle || m_state == State::Finished) {
                m_timer.SetDuration(m_selectedMinutes);
            }
            m_timer.Start();
            m_state = State::Running;
        }
        return true;
    case ButtonId::Stop:
        StopSession(false);
        return true;
    case ButtonId::Collapse:
        CollapseToCompact();
        return true;
    case ButtonId::None:
    default:
        return ContainsPoint(m_lastRect, x, y);
    }
}

bool PomodoroComponent::ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

D2D1_RECT_F PomodoroComponent::MakeRect(float left, float top, float width, float height) {
    return D2D1::RectF(left, top, left + width, top + height);
}

void PomodoroComponent::DrawExpanded(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    const float width = rect.right - rect.left;
    const float centerX = rect.left + width * 0.5f;
    const float pulse = (m_state == State::Running)
        ? (0.55f + 0.45f * std::sinf((float)(now % 1800) / 1800.0f * 6.2831853f))
        : 0.0f;

    ComPtr<ID2D1SolidColorBrush> panelBrush;
    ComPtr<ID2D1SolidColorBrush> outlineBrush;
    ComPtr<ID2D1SolidColorBrush> glowBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.032f * alpha), &panelBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f * alpha), &outlineBrush);
    ctx->CreateSolidColorBrush(AccentColor((0.08f + 0.06f * pulse) * alpha), &glowBrush);

    const D2D1_RECT_F heroRect = MakeRect(rect.left + 14.0f, rect.top + 10.0f, width - 28.0f, 170.0f);
    const D2D1_RECT_F footerRect = MakeRect(rect.left + 14.0f, rect.top + 186.0f, width - 28.0f, 46.0f);
    if (panelBrush) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(heroRect, 18.0f, 18.0f), panelBrush.Get());
        ctx->FillRoundedRectangle(D2D1::RoundedRect(footerRect, 18.0f, 18.0f), panelBrush.Get());
    }
    if (outlineBrush) {
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(heroRect, 18.0f, 18.0f), outlineBrush.Get(), 1.0f);
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(footerRect, 18.0f, 18.0f), outlineBrush.Get(), 1.0f);
    }

    const D2D1_RECT_F titleRect = MakeRect(heroRect.left + 16.0f, heroRect.top + 10.0f, 130.0f, 16.0f);
    DrawTextCentered(L"Pomodoro Focus", m_captionFormat ? m_captionFormat.Get() : m_res->subFormat, titleRect, m_res->grayBrush, alpha);

    {
        const D2D1_RECT_F stateLeftRect = MakeRect(heroRect.left + 8.0f, heroRect.top + 30.0f, 78.0f, 18.0f);
        ComPtr<ID2D1SolidColorBrush> stateBrush;
        const D2D1_COLOR_F stateColor = (m_state == State::Paused)
            ? PauseColor(alpha)
            : (m_state == State::Running ? AccentColor(alpha) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.62f * alpha));
        if (SUCCEEDED(ctx->CreateSolidColorBrush(stateColor, &stateBrush))) {
            DrawTextCentered(GetStatusText(), m_captionFormat ? m_captionFormat.Get() : m_res->subFormat, stateLeftRect, stateBrush.Get(), alpha);
        }
    }

    const D2D1_RECT_F durationRect = MakeRect(heroRect.right - 110.0f, heroRect.top + 10.0f, 94.0f, 16.0f);
    DrawTextCentered(GetDurationText(), m_captionFormat ? m_captionFormat.Get() : m_res->subFormat, durationRect, m_res->grayBrush, alpha);

    const float ringCenterY = heroRect.top + 88.0f;
    const float dialRadius = 48.0f;
    const float ringRadius = dialRadius + 10.0f + (m_state == State::Running ? pulse * 1.2f : 0.0f);
    if (glowBrush) {
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, ringCenterY), ringRadius + 10.0f, ringRadius + 10.0f), glowBrush.Get());
    }
    DrawRing(centerX, ringCenterY, ringRadius, m_timer.GetProgress(), alpha);
    DrawDial(centerX, ringCenterY, dialRadius, alpha, IsDialInteractive());
    DrawHand(centerX, ringCenterY, dialRadius - 6.0f, alpha);

    ComPtr<ID2D1SolidColorBrush> centerBrush;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(
        m_state == State::Paused ? PauseColor() : AccentColor(), &centerBrush))) {
        const D2D1_RECT_F centerTextRect = MakeRect(centerX - 62.0f, ringCenterY - 22.0f, 124.0f, 34.0f);
        DrawTextCentered(GetExpandedCenterText(), m_timerFormat ? m_timerFormat.Get() : m_res->titleFormat, centerTextRect, centerBrush.Get(), alpha);
    }

    for (const auto& button : m_buttons) {
        switch (button.id) {
        case ButtonId::CustomMinus:
            DrawButton(button, L"\u2212", m_hoveredButton == (int)button.id, alpha, 14.0f, m_res->titleFormat);
            break;
        case ButtonId::CustomPlus:
            DrawButton(button, L"+", m_hoveredButton == (int)button.id, alpha, 14.0f, m_res->titleFormat);
            break;
        case ButtonId::StartPause:
            DrawActionButton(button, button.id, true, m_hoveredButton == (int)button.id, alpha);
            break;
        case ButtonId::Stop:
            DrawActionButton(button, button.id, false, m_hoveredButton == (int)button.id, alpha);
            break;
        case ButtonId::Collapse:
            DrawActionButton(button, button.id, false, m_hoveredButton == (int)button.id, alpha);
            break;
        default:
            break;
        }
    }
    (void)now;
}

void PomodoroComponent::DrawCompact(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    if (!ctx) {
        return;
    }

    float height = rect.bottom - rect.top;
    float dotRadius = 3.2f;
    float dotX = rect.left + 15.0f;
    float dotY = rect.top + height * 0.5f;

    if (m_state == State::Running) {
        float pulse = 0.62f + 0.22f * std::sinf((float)(now % 1400) / 1400.0f * 6.2831853f);
        if (m_pulseBrush) {
            m_pulseBrush->SetOpacity(alpha * pulse);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotRadius, dotRadius), m_pulseBrush.Get());
        }
    }

    const float buttonSize = 22.0f;
    const float gap = 6.0f;
    const float right = rect.right - 10.0f;
    D2D1_RECT_F textRect = MakeRect(rect.left + 26.0f, rect.top + 5.0f, right - rect.left - 26.0f - (buttonSize * 2.0f + gap + 6.0f), height - 10.0f);
    ComPtr<ID2D1SolidColorBrush> textBrush;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(
        m_state == State::Paused ? PauseColor() : AccentColor(),
        &textBrush))) {
        DrawTextCentered(GetCompactText(), m_res->titleFormat, textRect, textBrush.Get(), alpha);
    } else {
        DrawTextCentered(GetCompactText(), m_res->titleFormat, textRect, m_res->whiteBrush, alpha);
    }

    for (const auto& button : m_buttons) {
        if (button.id == ButtonId::CompactPause) {
            DrawButton(button, m_state == State::Running ? L"\uE769" : L"\uE768", m_hoveredButton == (int)button.id, alpha, 12.0f);
        } else if (button.id == ButtonId::CompactStop) {
            DrawButton(button, L"\uE711", m_hoveredButton == (int)button.id, alpha, 12.0f);
        }
    }
}

void PomodoroComponent::DrawRing(float cx, float cy, float radius, float progress, float alpha) {
    if (!m_res || !m_res->d2dContext || !m_res->d2dFactory) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    progress = Clamp01(progress);

    ComPtr<ID2D1SolidColorBrush> ringBgBrush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.11f * alpha), &ringBgBrush))) {
        return;
    }
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), ringBgBrush.Get(), 2.6f);

    if (progress <= 0.001f) {
        return;
    }

    ComPtr<ID2D1SolidColorBrush> ringBrush;
    const D2D1_COLOR_F ringColor = (m_state == State::Paused) ? PauseColor(alpha) : AccentColor(alpha);
    if (FAILED(ctx->CreateSolidColorBrush(ringColor, &ringBrush))) {
        return;
    }

    if (progress >= 0.999f) {
        ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), ringBrush.Get(), 4.0f);
        return;
    }

    ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(m_res->d2dFactory->CreatePathGeometry(&geometry)) || !geometry) {
        return;
    }

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        return;
    }

    const float startAngle = -1.5707963f;
    const float endAngle = startAngle + progress * 6.2831853f;
    const D2D1_POINT_2F startPoint = D2D1::Point2F(cx + std::cosf(startAngle) * radius, cy + std::sinf(startAngle) * radius);
    const D2D1_POINT_2F endPoint = D2D1::Point2F(cx + std::cosf(endAngle) * radius, cy + std::sinf(endAngle) * radius);

    sink->BeginFigure(startPoint, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(
        endPoint,
        D2D1::SizeF(radius, radius),
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        progress > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close())) {
        ctx->DrawGeometry(geometry.Get(), ringBrush.Get(), 4.0f);
    }
}

void PomodoroComponent::DrawDial(float cx, float cy, float radius, float alpha, bool interactive) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> minuteBrush;
    ComPtr<ID2D1SolidColorBrush> selectedBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.12f, 0.92f * alpha), &minuteBrush);
    ctx->CreateSolidColorBrush(AccentColor(alpha), &selectedBrush);
    if (!minuteBrush || !selectedBrush) {
        return;
    }

    const int redTicks = GetRemainingMinuteTicks();
    for (int minute = 1; minute <= 60; ++minute) {
        const float angle = kDialStartAngle + (kTwoPi / 60.0f) * minute;
        const bool major = (minute % 5) == 0;
        const float inner = radius + 12.0f;
        const float outer = radius + (major ? 24.0f : 18.0f);
        const bool lit = minute <= redTicks;
        auto* brush = lit ? selectedBrush.Get() : minuteBrush.Get();
        const float stroke = lit ? (major ? 2.6f : 1.5f) : (major ? 2.0f : 1.2f);
        ctx->DrawLine(
            D2D1::Point2F(cx + std::cosf(angle) * inner, cy + std::sinf(angle) * inner),
            D2D1::Point2F(cx + std::cosf(angle) * outer, cy + std::sinf(angle) * outer),
            brush,
            stroke);
    }
}

void PomodoroComponent::DrawHand(float cx, float cy, float radius, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> handBrush;
    if (FAILED(ctx->CreateSolidColorBrush(m_state == State::Paused ? PauseColor(alpha) : AccentColor(alpha), &handBrush)) || !handBrush) {
        return;
    }

    const D2D1_POINT_2F end = D2D1::Point2F(cx + std::cosf(m_handAngleCurrent) * radius, cy + std::sinf(m_handAngleCurrent) * radius);
    ctx->DrawLine(D2D1::Point2F(cx, cy), end, handBrush.Get(), 2.4f);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 3.4f, 3.4f), handBrush.Get());
    ctx->FillEllipse(D2D1::Ellipse(end, 3.0f, 3.0f), handBrush.Get());
}

void PomodoroComponent::DrawButton(const ButtonRect& button, const wchar_t* icon, bool hovered, float alpha, float radius, IDWriteTextFormat* format) {
    if (!m_res || !m_res->d2dContext || !icon) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> fillBrush;
    const float base = hovered ? 0.20f : 0.09f;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, base * alpha), &fillBrush))) {
        D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(button.rect, radius, radius);
        ctx->FillRoundedRectangle(&rounded, fillBrush.Get());
    }
    DrawTextCentered(icon, format ? format : m_res->iconFormat, button.rect, m_res->whiteBrush, alpha);
}

void PomodoroComponent::DrawActionButton(const ButtonRect& button, ButtonId id, bool primary, bool hovered, float alpha) {
    if (!m_res || !m_res->d2dContext) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> fillBrush;
    ComPtr<ID2D1SolidColorBrush> strokeBrush;
    const D2D1_COLOR_F fill = primary
        ? AccentColor((hovered ? 1.0f : 0.92f) * alpha)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, (hovered ? 0.18f : 0.10f) * alpha);
    if (SUCCEEDED(ctx->CreateSolidColorBrush(fill, &fillBrush))) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(button.rect, 15.0f, 15.0f), fillBrush.Get());
    }
    if (!primary && SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, (hovered ? 0.12f : 0.08f) * alpha), &strokeBrush))) {
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(button.rect, 15.0f, 15.0f), strokeBrush.Get(), 1.0f);
    }
    switch (id) {
    case ButtonId::StartPause:
        if (m_state == State::Running) DrawPauseIcon(button.rect, alpha);
        else DrawPlayIcon(button.rect, alpha);
        break;
    case ButtonId::Stop:
        DrawCancelIcon(button.rect, alpha);
        break;
    case ButtonId::Collapse:
        DrawCollapseIcon(button.rect, alpha);
        break;
    default:
        break;
    }
}

void PomodoroComponent::DrawTextCentered(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
    ID2D1SolidColorBrush* brush, float alpha) {
    if (!m_res || !m_res->d2dContext || !m_res->dwriteFactory || !format || !brush || text.empty()) {
        return;
    }

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_res->dwriteFactory->CreateTextLayout(
        text.c_str(),
        (UINT32)text.length(),
        format,
        rect.right - rect.left,
        rect.bottom - rect.top,
        &layout)) || !layout) {
        return;
    }

    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    
    brush->SetOpacity(alpha);
    m_res->d2dContext->DrawTextLayout(
        D2D1::Point2F(rect.left, rect.top),
        layout.Get(),
        brush);
}

void PomodoroComponent::DrawPlayIcon(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &brush)) || !brush) {
        return;
    }
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(m_res->d2dFactory->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink)) || !sink) {
        return;
    }
    const D2D1_POINT_2F p1 = D2D1::Point2F(rect.left + 24.0f, rect.top + 12.0f);
    const D2D1_POINT_2F p2 = D2D1::Point2F(rect.right - 24.0f, (rect.top + rect.bottom) * 0.5f);
    const D2D1_POINT_2F p3 = D2D1::Point2F(rect.left + 24.0f, rect.bottom - 12.0f);
    sink->BeginFigure(p1, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(p2);
    sink->AddLine(p3);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    ctx->FillGeometry(geometry.Get(), brush.Get());
}

void PomodoroComponent::DrawPauseIcon(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &brush)) || !brush) {
        return;
    }
    ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left + 24.0f, rect.top + 12.0f, rect.left + 29.0f, rect.bottom - 12.0f), 2.0f, 2.0f), brush.Get());
    ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.right - 29.0f, rect.top + 12.0f, rect.right - 24.0f, rect.bottom - 12.0f), 2.0f, 2.0f), brush.Get());
}

void PomodoroComponent::DrawCancelIcon(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &brush)) || !brush) {
        return;
    }
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left + 21.0f, rect.top + 12.0f, rect.right - 21.0f, rect.bottom - 12.0f), 3.0f, 3.0f), brush.Get(), 1.7f);
}

void PomodoroComponent::DrawCollapseIcon(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &brush)) || !brush) {
        return;
    }
    const D2D1_POINT_2F p1 = D2D1::Point2F(rect.left + 20.0f, rect.top + 22.0f);
    const D2D1_POINT_2F p2 = D2D1::Point2F((rect.left + rect.right) * 0.5f, rect.top + 16.0f);
    const D2D1_POINT_2F p3 = D2D1::Point2F(rect.right - 20.0f, rect.top + 22.0f);
    ctx->DrawLine(p1, p2, brush.Get(), 1.7f);
    ctx->DrawLine(p2, p3, brush.Get(), 1.7f);
}

PomodoroComponent::ButtonId PomodoroComponent::HitTest(float x, float y) const {
    for (const auto& button : m_buttons) {
        if (ContainsPoint(button.rect, x, y)) {
            return button.id;
        }
    }
    if (ContainsPoint(m_lastRect, x, y)) {
        const float width = m_lastRect.right - m_lastRect.left;
        const float centerX = m_lastRect.left + width * 0.5f;
        const float centerY = m_lastRect.top + kDialCenterYOffset;
        const float dx = x - centerX;
        const float dy = y - centerY;
        const float distance = std::sqrtf(dx * dx + dy * dy);
        if (distance >= 28.0f && distance <= 58.0f) {
            return ButtonId::Dial;
        }
    }
    return ButtonId::None;
}

void PomodoroComponent::RebuildExpandedLayout(const D2D1_RECT_F& rect) {
    m_buttons.clear();
    const float customTop = rect.top + 118.0f;
    const float centerX = rect.left + (rect.right - rect.left) * 0.5f;
    m_buttons.push_back({ MakeRect(centerX - 104.0f, customTop, 28.0f, 28.0f), ButtonId::CustomMinus });
    m_buttons.push_back({ MakeRect(centerX + 76.0f, customTop, 28.0f, 28.0f), ButtonId::CustomPlus });

    const float actionTop = rect.top + 194.0f;
    const float buttonWidth = 54.0f;
    const float actionGap = 14.0f;
    const float actionHeight = 34.0f;
    const float totalWidth = buttonWidth * 3.0f + actionGap * 2.0f;
    const float actionLeft = rect.left + ((rect.right - rect.left) - totalWidth) * 0.5f;
    m_buttons.push_back({ MakeRect(actionLeft, actionTop, buttonWidth, actionHeight), ButtonId::StartPause });
    m_buttons.push_back({ MakeRect(actionLeft + buttonWidth + actionGap, actionTop, buttonWidth, actionHeight), ButtonId::Stop });
    m_buttons.push_back({ MakeRect(actionLeft + (buttonWidth + actionGap) * 2.0f, actionTop, buttonWidth, actionHeight), ButtonId::Collapse });
}

void PomodoroComponent::RebuildCompactLayout(const D2D1_RECT_F& rect) {
    m_buttons.clear();
    const float buttonSize = 22.0f;
    const float top = rect.top + ((rect.bottom - rect.top) - buttonSize) * 0.5f;
    const float gap = 6.0f;
    const float right = rect.right - 10.0f;
    m_buttons.push_back({ MakeRect(right - buttonSize * 2.0f - gap, top, buttonSize, buttonSize), ButtonId::CompactPause });
    m_buttons.push_back({ MakeRect(right - buttonSize, top, buttonSize, buttonSize), ButtonId::CompactStop });
}

void PomodoroComponent::AdjustCustomMinutes(int deltaMinutes) {
    SetSelectedMinutes((std::max)(5, (std::min)(60, m_selectedMinutes + deltaMinutes)), true);
}

void PomodoroComponent::SetSelectedMinutes(int minutes, bool animateHand) {
    minutes = (std::max)(5, (std::min)(60, ((minutes + 2) / 5) * 5));
    m_selectedMinutes = minutes;
    m_customMinutes = minutes;
    if (!HasActiveSession()) {
        m_timer.SetDuration(m_selectedMinutes);
        m_state = State::Setting;
    }
    const float rawTarget = MinutesToAngle(m_selectedMinutes);
    if (animateHand) {
        float delta = rawTarget - m_handAngleCurrent;
        while (delta > kPi) delta -= kTwoPi;
        while (delta < -kPi) delta += kTwoPi;
        m_handAngleTarget = m_handAngleCurrent + delta;
        m_handAnimating = true;
    } else {
        m_handAngleTarget = rawTarget;
        m_handAngleCurrent = rawTarget;
        m_handAnimating = false;
    }
}

float PomodoroComponent::MinutesToAngle(int minutes) const {
    if (minutes <= 0) minutes = 60;
    const int slot = (minutes / 5) % 12;
    return kDialStartAngle + kDialStep * slot;
}

int PomodoroComponent::PointToMinutes(float x, float y) const {
    const float width = m_lastRect.right - m_lastRect.left;
    const float centerX = m_lastRect.left + width * 0.5f;
    const float centerY = m_lastRect.top + kDialCenterYOffset;
    float angle = std::atan2f(y - centerY, x - centerX);
    angle -= kDialStartAngle;
    while (angle < 0.0f) angle += 6.2831853f;
    while (angle >= 6.2831853f) angle -= 6.2831853f;
    const int slot = static_cast<int>(std::round(angle / kDialStep)) % 12;
    const int minutes = (slot == 0) ? 60 : slot * 5;
    return minutes;
}

int PomodoroComponent::GetRemainingMinuteTicks() const {
    if (m_state == State::Running || m_state == State::Paused) {
        const int remainingTicks = (m_timer.GetRemainingSeconds() + 59) / 60;
        return (std::max)(0, (std::min)(m_selectedMinutes, remainingTicks));
    }
    return m_selectedMinutes;
}

bool PomodoroComponent::IsDialInteractive() const {
    return !HasActiveSession();
}

void PomodoroComponent::SyncStateFromTimer() {
    if (m_timer.IsRunning()) {
        m_state = State::Running;
        return;
    }

    if (m_timer.IsPaused() && m_timer.GetRemainingSeconds() > 0) {
        m_state = State::Paused;
    }
}

void PomodoroComponent::StopSession(bool finished) {
    m_timer.Pause();
    m_timer.Reset();
    m_expanded = false;
    m_state = finished ? State::Finished : State::Idle;
}

void PomodoroComponent::CollapseToCompact() {
    if (HasActiveSession()) {
        m_expanded = false;
        return;
    }
    m_expanded = false;
    m_state = State::Idle;
}

std::wstring PomodoroComponent::GetExpandedCenterText() const {
    if (HasActiveSession()) {
        return m_timer.GetDisplayText();
    }
    return FormatMinutes(m_selectedMinutes);
}

std::wstring PomodoroComponent::GetCompactText() const {
    return HasActiveSession() ? m_timer.GetDisplayText() : FormatMinutes(m_selectedMinutes);
}

std::wstring PomodoroComponent::GetStatusText() const {
    switch (m_state) {
    case State::Running:
        return L"Focus";
    case State::Paused:
        return L"Paused";
    case State::Finished:
        return L"Completed";
    case State::Setting:
        return L"Ready";
    case State::Idle:
    default:
        return L"Ready";
    }
}

std::wstring PomodoroComponent::GetDurationText() const {
    return FormatMinutes(m_selectedMinutes);
}
