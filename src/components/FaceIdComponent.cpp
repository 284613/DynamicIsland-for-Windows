#include "components/FaceIdComponent.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

float DegToRad(float deg) {
    return deg * kPi / 180.0f;
}

D2D1_POINT_2F PointOnCircle(float cx, float cy, float radius, float angle) {
    return D2D1::Point2F(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
}

} // namespace

void FaceIdComponent::OnAttach(SharedResources* res) {
    m_res = res;
    if (m_res && m_res->d2dContext) {
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.25f, 0.62f, 1.0f), &m_indigoBrush);
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.72f, 0.38f, 1.0f), &m_greenBrush);
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.25f, 1.0f), &m_redBrush);
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.24f), &m_softBrush);
    }
    if (m_res && m_res->dwriteFactory) {
        m_res->dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"zh-cn", &m_labelFormat);
        if (m_labelFormat) {
            m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_labelFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void FaceIdComponent::SetState(FaceIdState state, std::wstring text) {
    if (m_state != state) {
        m_startedMs = GetTickCount64();
        m_phase = 0.0f;
    }
    m_state = state;
    m_text = std::move(text);
}

void FaceIdComponent::Update(float deltaTime) {
    if (m_state != FaceIdState::Hidden) {
        m_phase += deltaTime;
    }
}

void FaceIdComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res || !m_res->d2dContext || m_state == FaceIdState::Hidden) {
        return;
    }

    switch (m_state) {
    case FaceIdState::Scanning:
        DrawScanning(rect, contentAlpha, currentTimeMs);
        break;
    case FaceIdState::Success:
        DrawSuccess(rect, contentAlpha, currentTimeMs);
        break;
    case FaceIdState::Failed:
        DrawFailed(rect, contentAlpha, currentTimeMs);
        break;
    default:
        break;
    }
}

void FaceIdComponent::DrawScanning(const D2D1_RECT_F& rect, float alpha, ULONGLONG) {
    auto* ctx = m_res->d2dContext;
    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float radius = height * 0.5f;
    const float cx = rect.left + 32.0f;
    const float cy = rect.top + height * 0.5f;

    m_indigoBrush->SetOpacity(alpha * 0.28f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, height * 0.5f, height * 0.5f), m_indigoBrush.Get());

    m_softBrush->SetOpacity(alpha * 0.28f);
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius - 12.0f, radius - 12.0f), m_softBrush.Get(), 2.0f);

    if (m_res->whiteBrush) {
        m_res->whiteBrush->SetOpacity(alpha);
        const float base = m_phase * DegToRad(60.0f);
        DrawArc(cx, cy, radius - 12.0f, base, DegToRad(82.0f), m_res->whiteBrush, 2.6f);
        DrawArc(cx, cy, radius - 12.0f, base + DegToRad(122.0f), DegToRad(72.0f), m_res->whiteBrush, 2.6f);
        DrawArc(cx, cy, radius - 12.0f, base + DegToRad(236.0f), DegToRad(64.0f), m_res->whiteBrush, 2.6f);
    }

    DrawCenteredText(m_text.empty() ? L"正在识别" : m_text,
        D2D1::RectF(rect.left + 58.0f, rect.top, rect.right - 32.0f, rect.bottom), alpha);

    if (m_res->grayBrush) {
        m_res->grayBrush->SetOpacity(alpha * 0.75f);
        D2D1_RECT_F chevron = D2D1::RectF(rect.right - 27.0f, rect.top + 18.0f, rect.right - 11.0f, rect.bottom - 18.0f);
        ctx->DrawLine(D2D1::Point2F(chevron.left, chevron.top), D2D1::Point2F((chevron.left + chevron.right) * 0.5f, chevron.bottom), m_res->grayBrush, 1.8f);
        ctx->DrawLine(D2D1::Point2F((chevron.left + chevron.right) * 0.5f, chevron.bottom), D2D1::Point2F(chevron.right, chevron.top), m_res->grayBrush, 1.8f);
    }
}

void FaceIdComponent::DrawSuccess(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    const float height = rect.bottom - rect.top;
    const float cx = rect.left + 32.0f;
    const float cy = rect.top + height * 0.5f;
    const float t = Clamp01(static_cast<float>(now - m_startedMs) / 260.0f);
    const float pulse = Clamp01(static_cast<float>(now - m_startedMs) / 1000.0f);

    m_greenBrush->SetOpacity(alpha * 0.32f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, height * 0.5f, height * 0.5f), m_greenBrush.Get());
    m_greenBrush->SetOpacity(alpha * (0.35f * (1.0f - pulse)));
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 14.0f + pulse * 10.0f, 14.0f + pulse * 10.0f), m_greenBrush.Get(), 2.0f);

    if (m_res->whiteBrush) {
        m_res->whiteBrush->SetOpacity(alpha);
        D2D1_POINT_2F a = D2D1::Point2F(cx - 9.0f, cy + 0.5f);
        D2D1_POINT_2F b = D2D1::Point2F(cx - 2.0f, cy + 7.0f);
        D2D1_POINT_2F c = D2D1::Point2F(cx + 11.0f, cy - 8.0f);
        if (t < 0.5f) {
            float local = t / 0.5f;
            ctx->DrawLine(a, D2D1::Point2F(a.x + (b.x - a.x) * local, a.y + (b.y - a.y) * local), m_res->whiteBrush, 3.0f);
        } else {
            ctx->DrawLine(a, b, m_res->whiteBrush, 3.0f);
            float local = (t - 0.5f) / 0.5f;
            ctx->DrawLine(b, D2D1::Point2F(b.x + (c.x - b.x) * local, b.y + (c.y - b.y) * local), m_res->whiteBrush, 3.0f);
        }
    }

    DrawCenteredText(m_text.empty() ? L"已解锁" : m_text,
        D2D1::RectF(rect.left + 58.0f, rect.top, rect.right - 18.0f, rect.bottom), alpha);
}

void FaceIdComponent::DrawFailed(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    const float height = rect.bottom - rect.top;
    const float elapsed = static_cast<float>(now - m_startedMs);
    const float shake = elapsed < 320.0f ? std::sin(elapsed / 80.0f * kTwoPi) * 6.0f : 0.0f;
    const float cx = rect.left + 32.0f + shake;
    const float cy = rect.top + height * 0.5f;

    D2D1_RECT_F shifted = D2D1::RectF(rect.left + shake, rect.top, rect.right + shake, rect.bottom);
    m_redBrush->SetOpacity(alpha * 0.30f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(shifted, height * 0.5f, height * 0.5f), m_redBrush.Get());

    if (m_res->whiteBrush) {
        m_res->whiteBrush->SetOpacity(alpha);
        ctx->DrawLine(D2D1::Point2F(cx - 9.0f, cy - 9.0f), D2D1::Point2F(cx + 9.0f, cy + 9.0f), m_res->whiteBrush, 3.0f);
        ctx->DrawLine(D2D1::Point2F(cx + 9.0f, cy - 9.0f), D2D1::Point2F(cx - 9.0f, cy + 9.0f), m_res->whiteBrush, 3.0f);
    }

    DrawCenteredText(m_text.empty() ? L"识别失败" : m_text,
        D2D1::RectF(rect.left + 58.0f + shake, rect.top, rect.right - 18.0f + shake, rect.bottom), alpha);
}

void FaceIdComponent::DrawArc(float cx, float cy, float radius, float startAngle, float sweepAngle,
    ID2D1Brush* brush, float strokeWidth) {
    if (!m_res || !m_res->d2dFactory || !m_res->d2dContext || !brush) {
        return;
    }

    ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(m_res->d2dFactory->CreatePathGeometry(&geometry))) {
        return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink))) {
        return;
    }

    D2D1_POINT_2F start = PointOnCircle(cx, cy, radius, startAngle);
    D2D1_POINT_2F end = PointOnCircle(cx, cy, radius, startAngle + sweepAngle);
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(
        end,
        D2D1::SizeF(radius, radius),
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        std::fabs(sweepAngle) > kPi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close())) {
        m_res->d2dContext->DrawGeometry(geometry.Get(), brush, strokeWidth);
    }
}

void FaceIdComponent::DrawCenteredText(const std::wstring& text, const D2D1_RECT_F& rect, float alpha) {
    if (!m_res || !m_res->d2dContext || !m_res->whiteBrush || !m_labelFormat) {
        return;
    }
    m_res->whiteBrush->SetOpacity(alpha);
    m_res->d2dContext->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()),
        m_labelFormat.Get(), rect, m_res->whiteBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
