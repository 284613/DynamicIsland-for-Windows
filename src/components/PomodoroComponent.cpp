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

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

std::wstring FormatMinutes(int minutes) {
    return std::to_wstring(minutes) + L" min";
}
}

PomodoroComponent::PomodoroComponent() = default;

void PomodoroComponent::OnAttach(SharedResources* res) {
    m_res = res;
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
    return IsRunning();
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
    (void)deltaTime;
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
    case ButtonId::Preset25:
        ApplyPresetMinutes(25);
        return true;
    case ButtonId::Preset15:
        ApplyPresetMinutes(15);
        return true;
    case ButtonId::Preset5:
        ApplyPresetMinutes(5);
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
    float width = rect.right - rect.left;
    float centerX = rect.left + width * 0.5f;

    D2D1_RECT_F titleRect = MakeRect(rect.left + 18.0f, rect.top + 12.0f, width - 36.0f, 22.0f);
    DrawTextCentered(L"Pomodoro", m_res->titleFormat, titleRect, m_res->whiteBrush, alpha);

    float ringCenterY = rect.top + 72.0f;
    float ringRadius = 28.0f;
    DrawRing(centerX, ringCenterY, ringRadius, m_timer.GetProgress(), alpha);

    D2D1_RECT_F centerTextRect = MakeRect(centerX - 50.0f, ringCenterY - 13.0f, 100.0f, 26.0f);
    ComPtr<ID2D1SolidColorBrush> centerBrush;
    if (m_res->d2dContext && SUCCEEDED(m_res->d2dContext->CreateSolidColorBrush(
        (m_state == State::Paused)
            ? D2D1::ColorF(kPauseR, kPauseG, kPauseB, 1.0f)
            : D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, 1.0f),
        &centerBrush))) {
        DrawTextCentered(GetExpandedCenterText(), m_res->titleFormat, centerTextRect, centerBrush.Get(), alpha);
    } else {
        DrawTextCentered(GetExpandedCenterText(), m_res->titleFormat, centerTextRect, m_res->whiteBrush, alpha);
    }

    float presetTop = rect.top + 122.0f;
    for (const auto& button : m_buttons) {
        switch (button.id) {
        case ButtonId::Preset25:
            DrawLabelButton(button, L"25 min", m_selectedMinutes == 25 && !HasActiveSession(), m_hoveredButton == (int)button.id, alpha);
            break;
        case ButtonId::Preset15:
            DrawLabelButton(button, L"15 min", m_selectedMinutes == 15 && !HasActiveSession(), m_hoveredButton == (int)button.id, alpha);
            break;
        case ButtonId::Preset5:
            DrawLabelButton(button, L"5 min", m_selectedMinutes == 5 && !HasActiveSession(), m_hoveredButton == (int)button.id, alpha);
            break;
        case ButtonId::CustomMinus:
            DrawButton(button, L"\u2212", m_hoveredButton == (int)button.id, alpha, 13.0f, m_res->titleFormat);
            break;
        case ButtonId::CustomPlus:
            DrawButton(button, L"+", m_hoveredButton == (int)button.id, alpha, 13.0f, m_res->titleFormat);
            break;
        case ButtonId::StartPause:
            DrawButton(button, m_state == State::Running ? L"\uE769" : L"\uE768", m_hoveredButton == (int)button.id, alpha, 16.0f);
            break;
        case ButtonId::Stop:
            DrawButton(button, L"\uE71A", m_hoveredButton == (int)button.id, alpha, 16.0f);
            break;
        case ButtonId::Collapse:
            DrawButton(button, L"\uE70E", m_hoveredButton == (int)button.id, alpha, 16.0f);
            break;
        default:
            break;
        }
    }

    const float customRowTop = presetTop + 40.0f;
    D2D1_RECT_F customBgRect = MakeRect(centerX - 68.0f, customRowTop, 136.0f, 26.0f);
    if (m_res->d2dContext) {
        ComPtr<ID2D1SolidColorBrush> customBgBrush;
        if (SUCCEEDED(m_res->d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f * alpha), &customBgBrush))) {
            D2D1_ROUNDED_RECT customRounded = D2D1::RoundedRect(customBgRect, 13.0f, 13.0f);
            m_res->d2dContext->FillRoundedRectangle(&customRounded, customBgBrush.Get());
        }
    }
    D2D1_RECT_F customTextRect = MakeRect(centerX - 60.0f, customRowTop + 1.0f, 120.0f, 24.0f);
    DrawTextCentered(L"Custom " + FormatMinutes(m_customMinutes), m_res->subFormat, customTextRect, m_res->grayBrush, alpha);
    (void)now;
}

void PomodoroComponent::DrawCompact(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    if (!ctx) {
        return;
    }

    float height = rect.bottom - rect.top;
    float dotRadius = 4.0f;
    float dotX = rect.left + 16.0f;
    float dotY = rect.top + height * 0.5f;

    if (m_state == State::Running) {
        float pulse = 0.55f + 0.45f * std::sinf((float)(now % 1200) / 1200.0f * 6.2831853f);
        if (m_res->themeBrush) {
            m_res->themeBrush->SetColor(D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, alpha * pulse));
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotRadius, dotRadius), m_res->themeBrush);
        }
    }

    D2D1_RECT_F textRect = MakeRect(rect.left + 26.0f, rect.top + 6.0f, 84.0f, height - 12.0f);
    ComPtr<ID2D1SolidColorBrush> textBrush;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(
        (m_state == State::Paused)
            ? D2D1::ColorF(kPauseR, kPauseG, kPauseB, 1.0f)
            : D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, 1.0f),
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
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f * alpha), &ringBgBrush))) {
        return;
    }
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), ringBgBrush.Get(), 2.0f);

    if (progress <= 0.001f) {
        return;
    }

    ComPtr<ID2D1SolidColorBrush> ringBrush;
    const D2D1_COLOR_F ringColor = (m_state == State::Paused)
        ? D2D1::ColorF(kPauseR, kPauseG, kPauseB, alpha)
        : D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, alpha);
    if (FAILED(ctx->CreateSolidColorBrush(ringColor, &ringBrush))) {
        return;
    }

    if (progress >= 0.999f) {
        ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), ringBrush.Get(), 3.0f);
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
        ctx->DrawGeometry(geometry.Get(), ringBrush.Get(), 3.0f);
    }
}

void PomodoroComponent::DrawButton(const ButtonRect& button, const wchar_t* icon, bool hovered, float alpha, float radius, IDWriteTextFormat* format) {
    if (!m_res || !m_res->d2dContext || !icon) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> fillBrush;
    const float base = hovered ? 0.24f : 0.12f;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, base * alpha), &fillBrush))) {
        D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(button.rect, radius, radius);
        ctx->FillRoundedRectangle(&rounded, fillBrush.Get());
    }
    DrawTextCentered(icon, format ? format : m_res->iconFormat, button.rect, m_res->whiteBrush, alpha);
}

void PomodoroComponent::DrawLabelButton(const ButtonRect& button, const std::wstring& text, bool selected, bool hovered, float alpha) {
    if (!m_res || !m_res->d2dContext) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> fillBrush;
    D2D1_COLOR_F fill = selected
        ? D2D1::ColorF(kTomatoR, kTomatoG, kTomatoB, alpha)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, (hovered ? 0.18f : 0.10f) * alpha);
    if (SUCCEEDED(ctx->CreateSolidColorBrush(fill, &fillBrush))) {
        D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(button.rect, 11.0f, 11.0f);
        ctx->FillRoundedRectangle(&rounded, fillBrush.Get());
    }

    DrawTextCentered(text, m_res->subFormat, button.rect, m_res->whiteBrush, alpha);
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

PomodoroComponent::ButtonId PomodoroComponent::HitTest(float x, float y) const {
    for (const auto& button : m_buttons) {
        if (ContainsPoint(button.rect, x, y)) {
            return button.id;
        }
    }
    return ButtonId::None;
}

void PomodoroComponent::RebuildExpandedLayout(const D2D1_RECT_F& rect) {
    m_buttons.clear();
    float top = rect.top + 122.0f;
    float chipWidth = 64.0f;
    float chipGap = 10.0f;
    float totalChipWidth = chipWidth * 3.0f + chipGap * 2.0f;
    float chipLeft = rect.left + ((rect.right - rect.left) - totalChipWidth) * 0.5f;
    m_buttons.push_back({ MakeRect(chipLeft, top, chipWidth, 26.0f), ButtonId::Preset25 });
    m_buttons.push_back({ MakeRect(chipLeft + chipWidth + chipGap, top, chipWidth, 26.0f), ButtonId::Preset15 });
    m_buttons.push_back({ MakeRect(chipLeft + (chipWidth + chipGap) * 2.0f, top, chipWidth, 26.0f), ButtonId::Preset5 });

    float customTop = top + 40.0f;
    float centerX = rect.left + (rect.right - rect.left) * 0.5f;
    m_buttons.push_back({ MakeRect(centerX - 100.0f, customTop, 28.0f, 26.0f), ButtonId::CustomMinus });
    m_buttons.push_back({ MakeRect(centerX + 72.0f, customTop, 28.0f, 26.0f), ButtonId::CustomPlus });

    float actionTop = rect.bottom - 46.0f;
    float actionWidth = 52.0f;
    float actionGap = 16.0f;
    float actionHeight = 32.0f;
    float totalWidth = actionWidth * 3.0f + actionGap * 2.0f;
    float actionLeft = rect.left + ((rect.right - rect.left) - totalWidth) * 0.5f;
    m_buttons.push_back({ MakeRect(actionLeft, actionTop, actionWidth, actionHeight), ButtonId::StartPause });
    m_buttons.push_back({ MakeRect(actionLeft + actionWidth + actionGap, actionTop, actionWidth, actionHeight), ButtonId::Stop });
    m_buttons.push_back({ MakeRect(actionLeft + (actionWidth + actionGap) * 2.0f, actionTop, actionWidth, actionHeight), ButtonId::Collapse });
}

void PomodoroComponent::RebuildCompactLayout(const D2D1_RECT_F& rect) {
    m_buttons.clear();
    const float top = rect.top + 7.0f;
    const float buttonSize = 24.0f;
    const float gap = 6.0f;
    const float right = rect.right - 10.0f;
    m_buttons.push_back({ MakeRect(right - buttonSize * 2.0f - gap, top, buttonSize, buttonSize), ButtonId::CompactPause });
    m_buttons.push_back({ MakeRect(right - buttonSize, top, buttonSize, buttonSize), ButtonId::CompactStop });
}

void PomodoroComponent::ApplyPresetMinutes(int minutes) {
    m_selectedMinutes = minutes;
    if (!HasActiveSession()) {
        m_timer.SetDuration(minutes);
        m_state = State::Setting;
    }
}

void PomodoroComponent::AdjustCustomMinutes(int deltaMinutes) {
    m_customMinutes = (std::max)(5, (std::min)(60, m_customMinutes + deltaMinutes));
    m_selectedMinutes = m_customMinutes;
    if (!HasActiveSession()) {
        m_timer.SetDuration(m_selectedMinutes);
        m_state = State::Setting;
    }
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
