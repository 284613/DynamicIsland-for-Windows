#include "components/FaceIdComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

float SmoothStep(float value) {
    value = Clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

float EaseInOutCubic(float x) {
    x = Clamp01(x);
    return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
}

float EaseOutBack(float x) {
    x = Clamp01(x);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(x - 1.0f, 3.0f) + c1 * std::pow(x - 1.0f, 2.0f);
}

float DegToRad(float deg) {
    return deg * kPi / 180.0f;
}

D2D1_POINT_2F PointOnCircle(float cx, float cy, float radius, float angle) {
    return D2D1::Point2F(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
}

void DrawMorphingFaceBox(ID2D1Factory1* factory, ID2D1RenderTarget* target, ID2D1Brush* brush,
    float cx, float cy, float size, float strokeWidth, float mergeProgress) {
    if (!factory || !target || !brush) return;

    const float half = size * 0.5f;
    const float gap = (1.0f - mergeProgress) * (size * 0.25f);
    const float r = size * 0.08f + mergeProgress * (size * 0.42f);

    ComPtr<ID2D1PathGeometry> geometry;
    factory->CreatePathGeometry(&geometry);
    ComPtr<ID2D1GeometrySink> sink;
    geometry->Open(&sink);

    // Top-Left
    sink->BeginFigure(D2D1::Point2F(cx - half, cy - gap), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx - half, cy - half + r));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - half + r, cy - half), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine(D2D1::Point2F(cx - gap, cy - half));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    // Top-Right
    sink->BeginFigure(D2D1::Point2F(cx + gap, cy - half), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx + half - r, cy - half));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + half, cy - half + r), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine(D2D1::Point2F(cx + half, cy - gap));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    // Bottom-Right
    sink->BeginFigure(D2D1::Point2F(cx + half, cy + gap), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx + half, cy + half - r));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + half - r, cy + half), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine(D2D1::Point2F(cx + gap, cy + half));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    // Bottom-Left
    sink->BeginFigure(D2D1::Point2F(cx - gap, cy + half), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx - half + r, cy + half));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - half, cy + half - r), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine(D2D1::Point2F(cx - half, cy + gap));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    sink->Close();

    ComPtr<ID2D1StrokeStyle> strokeStyle;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND
    );
    factory->CreateStrokeStyle(props, nullptr, 0, &strokeStyle);

    target->DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle.Get());
}

void DrawFaceInside(ID2D1Factory1* factory, ID2D1RenderTarget* target, ID2D1Brush* brush,
    float cx, float cy, float size, float strokeWidth) {
    if (!factory || !target || !brush) return;

    ComPtr<ID2D1PathGeometry> geometry;
    factory->CreatePathGeometry(&geometry);
    ComPtr<ID2D1GeometrySink> sink;
    geometry->Open(&sink);

    // Left eye
    sink->BeginFigure(D2D1::Point2F(cx - size * 0.16f, cy - size * 0.16f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx - size * 0.16f, cy - size * 0.02f));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    // Right eye
    sink->BeginFigure(D2D1::Point2F(cx + size * 0.16f, cy - size * 0.16f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx + size * 0.16f, cy - size * 0.02f));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    // Nose
    sink->BeginFigure(D2D1::Point2F(cx, cy - size * 0.04f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx, cy + size * 0.1f));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    // Smile
    sink->BeginFigure(D2D1::Point2F(cx - size * 0.22f, cy + size * 0.2f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
        D2D1::Point2F(cx, cy + size * 0.36f),
        D2D1::Point2F(cx + size * 0.22f, cy + size * 0.2f)
    ));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);

    sink->Close();

    ComPtr<ID2D1StrokeStyle> strokeStyle;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND
    );
    factory->CreateStrokeStyle(props, nullptr, 0, &strokeStyle);

    target->DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle.Get());
}

void DrawCheckmarkCircle(ID2D1Factory1* factory, ID2D1RenderTarget* target, ID2D1Brush* brush,
    float cx, float cy, float size, float strokeWidth) {
    if (!factory || !target || !brush) return;

    D2D1_ELLIPSE circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), size * 0.5f, size * 0.5f);
    target->DrawEllipse(&circle, brush, strokeWidth);

    ComPtr<ID2D1PathGeometry> geometry;
    factory->CreatePathGeometry(&geometry);
    ComPtr<ID2D1GeometrySink> sink;
    geometry->Open(&sink);

    sink->BeginFigure(D2D1::Point2F(cx - size * 0.15f, cy + size * 0.05f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx - size * 0.05f, cy + size * 0.15f));
    sink->AddLine(D2D1::Point2F(cx + size * 0.22f, cy - size * 0.12f));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    ComPtr<ID2D1StrokeStyle> strokeStyle;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND
    );
    factory->CreateStrokeStyle(props, nullptr, 0, &strokeStyle);

    target->DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle.Get());
}

} // namespace

void FaceIdComponent::OnAttach(SharedResources* res) {
    m_res = res;
    if (m_res && m_res->d2dContext) {
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.45f, 1.0f, 1.0f), &m_indigoBrush);
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.86f, 0.45f, 1.0f), &m_greenBrush);
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.28f, 0.32f, 1.0f), &m_redBrush);
        m_res->d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.24f), &m_softBrush);
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
    if (!ctx || !m_greenBrush) {
        return;
    }

    const float height = rect.bottom - rect.top;
    // 扫描时在左侧
    const float cx = rect.left + height;
    const float cy = rect.top + height * 0.5f;
    const float faceSize = (std::min)(32.0f, height - 6.0f);
    const float radius = faceSize * 0.6f;

    m_greenBrush->SetOpacity(alpha * 0.96f);
    DrawMorphingFaceBox(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f, 0.0f);
    DrawFaceInside(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f);

    m_greenBrush->SetOpacity(alpha * 0.6f);
    const float base = m_phase * DegToRad(128.0f);
    DrawArc(cx, cy, radius, base, DegToRad(78.0f), m_greenBrush.Get(), 2.0f);
    DrawArc(cx, cy, radius, base + DegToRad(180.0f), DegToRad(78.0f), m_greenBrush.Get(), 2.0f);
}

void FaceIdComponent::DrawSuccess(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    if (!ctx || !m_greenBrush) {
        return;
    }

    const float height = rect.bottom - rect.top;
    const float elapsed = static_cast<float>(now - m_startedMs);
    
    // Animation timing with delay for Windows lockscreen fade-in
    const float delayMs = 1100.0f;
    const float morphDuration = 250.0f;
    const float flipDuration = 450.0f;

    const float morphTime = Clamp01((elapsed - delayMs) / morphDuration);
    const float mergeProgress = EaseInOutCubic(morphTime);
    
    const float flipTime = Clamp01((elapsed - delayMs - morphDuration) / flipDuration);
    const float flipProgress = EaseInOutCubic(flipTime);
    
    const float pulse = Clamp01((elapsed - delayMs) / 780.0f);

    const float startX = rect.left + height;
    const float endX = rect.right - height;
    // 翻转时从左侧平滑移动到右侧
    const float cx = startX + (endX - startX) * flipProgress;
    const float cy = rect.top + height * 0.5f;
    const float faceSize = (std::min)(32.0f, height - 6.0f);

    // Pulse effect
    m_greenBrush->SetOpacity(alpha * 0.15f * (1.0f - pulse));
    D2D1_ELLIPSE pulseRing = D2D1::Ellipse(D2D1::Point2F(cx, cy),
        faceSize * 0.55f + pulse * 9.0f,
        faceSize * 0.55f + pulse * 9.0f);
    ctx->DrawEllipse(&pulseRing, m_greenBrush.Get(), 1.9f);

    m_greenBrush->SetOpacity(alpha * 0.95f);

    D2D1_MATRIX_3X2_F oldTransform;
    ctx->GetTransform(&oldTransform);

    if (flipProgress > 0.0f) {
        float sx = std::cos(flipProgress * kPi); // 1.0 -> -1.0
        // Apply pop-out/bouncy scale effect to the checkmark phase
        float scalePop = 1.0f;
        if (flipProgress > 0.5f) {
            float checkTime = (flipProgress - 0.5f) * 2.0f;
            scalePop = 0.8f + 0.2f * EaseOutBack(checkTime);
        }

        D2D1_MATRIX_3X2_F flipTransform = D2D1::Matrix3x2F::Scale(
            D2D1::SizeF(std::abs(sx) * scalePop, scalePop),
            D2D1::Point2F(cx, cy)
        );
        ctx->SetTransform(flipTransform * oldTransform);

        if (sx > 0.0f) {
            // First half of flip: showing the circle face
            DrawMorphingFaceBox(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f, mergeProgress);
            DrawFaceInside(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f);
        } else {
            // Second half of flip: showing the checkmark circle
            DrawCheckmarkCircle(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f);
        }
    } else {
        // Before flip starts: morphing into circle
        DrawMorphingFaceBox(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f, mergeProgress);
        DrawFaceInside(m_res->d2dFactory, ctx, m_greenBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f);
    }

    // Restore transform
    ctx->SetTransform(oldTransform);
}

void FaceIdComponent::DrawFailed(const D2D1_RECT_F& rect, float alpha, ULONGLONG now) {
    auto* ctx = m_res->d2dContext;
    if (!ctx || !m_redBrush) {
        return;
    }

    const float height = rect.bottom - rect.top;
    const float elapsed = static_cast<float>(now - m_startedMs);
    const float shake = elapsed < 320.0f ? std::sin(elapsed / 72.0f * kTwoPi) * 3.0f : 0.0f;
    // 失败时保持在左侧并抖动
    const float cx = rect.left + height + shake;
    const float cy = rect.top + height * 0.5f;
    const float faceSize = (std::min)(32.0f, height - 6.0f);

    m_redBrush->SetOpacity(alpha * 0.85f);
    DrawMorphingFaceBox(m_res->d2dFactory, ctx, m_redBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f, 0.0f);
    DrawFaceInside(m_res->d2dFactory, ctx, m_redBrush.Get(), cx, cy, faceSize * 0.75f, 1.8f);

    m_redBrush->SetOpacity(alpha * 0.95f);
    ComPtr<ID2D1StrokeStyle> strokeStyle;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND
    );
    m_res->d2dFactory->CreateStrokeStyle(props, nullptr, 0, &strokeStyle);
    
    ctx->DrawLine(D2D1::Point2F(cx - 10.0f, cy - 10.0f), D2D1::Point2F(cx + 10.0f, cy + 10.0f), m_redBrush.Get(), 2.5f, strokeStyle.Get());
    ctx->DrawLine(D2D1::Point2F(cx + 10.0f, cy - 10.0f), D2D1::Point2F(cx - 10.0f, cy + 10.0f), m_redBrush.Get(), 2.5f, strokeStyle.Get());
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
        ComPtr<ID2D1StrokeStyle> strokeStyle;
        D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND
        );
        m_res->d2dFactory->CreateStrokeStyle(props, nullptr, 0, &strokeStyle);
        m_res->d2dContext->DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle.Get());
    }
}
