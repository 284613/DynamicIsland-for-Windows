#include "components/WeatherComponent.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
namespace {
constexpr float kPi = 3.14159265f;
float Clamp01(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }
float EaseInOut(float x) { return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f; }
}
void WeatherComponent::OnAttach(SharedResources* res) { m_res = res; }
void WeatherComponent::Update(float deltaTime) {
    m_animPhase += deltaTime * 1.5f;
    if (m_isViewTransitioning && m_viewTransitionDuration > 0.0f) {
        m_viewTransitionProgress += deltaTime / m_viewTransitionDuration;
        if (m_viewTransitionProgress >= 1.0f) {
            m_viewTransitionProgress = 1.0f; m_viewMode = m_targetViewMode;
            m_transitionFromMode = m_targetViewMode; m_isViewTransitioning = false;
        }
    }
}
void WeatherComponent::SetWeatherData(const std::wstring& loc, const std::wstring& d, float t, const std::wstring& id, const std::vector<HourlyForecast>& h, const std::vector<DailyForecast>& dy, bool av) {
    WeatherType nt = MapWeatherDescToType(d); if (nt != m_weatherType) { m_weatherType = nt; ResetAnimation(); }
    m_locationText = loc; m_desc = d; m_temp = t; m_iconId = id; m_hourly = h; m_daily = dy; m_available = av;
}
void WeatherComponent::SetViewMode(WeatherViewMode m) { BeginViewTransition(m); }
void WeatherComponent::Draw(const D2D1_RECT_F& r, float a, ULONGLONG t) {
    if (!m_res || !m_isExpanded) return;
    if (!m_available) { DrawUnavailable(r, a, t); return; }
    if (!m_isViewTransitioning) { DrawWeatherView(m_viewMode, r, a, t); return; }
    float e = EaseInOut(Clamp01(m_viewTransitionProgress));
    float ey = -e * 8.0f, ty = (1.0f - e) * 15.0f;
    D2D1_RECT_F fr = r; fr.top += ey; fr.bottom += ey; DrawWeatherView(m_transitionFromMode, fr, a * (1.0f - e), t);
    D2D1_RECT_F tr = r; tr.top += ty; tr.bottom += ty; DrawWeatherView(m_targetViewMode, tr, a * e, t);
}
void WeatherComponent::DrawCompact(float ix, float iy, float is, float a, ULONGLONG t) {
    if (!m_res) return; DrawWeatherIcon(ix, iy, is, a, t);
    if (m_res->subFormat && m_available) {
        std::wstring s = std::to_wstring((int)m_temp) + L"\u00B0";
        auto l = GetOrCreateTextLayout(s, m_res->subFormat, 60.0f, L"ct");
        DWRITE_TEXT_METRICS m; l->GetMetrics(&m); m_res->whiteBrush->SetOpacity(a * 0.9f);
        m_res->d2dContext->DrawTextLayout(D2D1::Point2F(ix - m.width - 6.0f, iy + (is - m.height) * 0.5f), l.Get(), m_res->whiteBrush);
    }
}
bool WeatherComponent::OnMouseWheel(float x, float y, int d) {
    if (!m_isExpanded) return false;
    if (d < 0) BeginViewTransition(WeatherViewMode::Daily); else BeginViewTransition(WeatherViewMode::Hourly);
    return true;
}
void WeatherComponent::DrawWeatherView(WeatherViewMode m, const D2D1_RECT_F& r, float a, ULONGLONG t) {
    if (a <= 0.01f) return;
    if (m == WeatherViewMode::Daily) DrawWeatherDaily(r, a, t); else DrawWeatherExpanded(r, a, t);
}
void WeatherComponent::BeginViewTransition(WeatherViewMode tm) {
    if (m_isViewTransitioning) {
        if (tm == m_targetViewMode) return;
        m_transitionFromMode = m_targetViewMode; m_targetViewMode = tm;
        m_viewTransitionProgress = 1.0f - Clamp01(m_viewTransitionProgress); return;
    }
    if (tm == m_viewMode) return;
    m_transitionFromMode = m_viewMode; m_targetViewMode = tm; m_viewTransitionProgress = 0.0f; m_isViewTransitioning = true;
}
void WeatherComponent::DrawWeatherIcon(float x, float y, float s, float a, ULONGLONG ct) {
    if (!m_res) return; auto* ctx = m_res->d2dContext; auto* fac = m_res->d2dFactory;
    float cx = x + s * 0.5f, cy = y + s * 0.5f, r = s * 0.45f, t = m_animPhase;
    auto DC = [&](float ccx, float ccy, float cs, float op, bool th = false) {
        ComPtr<ID2D1SolidColorBrush> b; ctx->CreateSolidColorBrush(th ? D2D1::ColorF(0.3f, 0.35f, 0.4f) : D2D1::ColorF(0.95f, 0.96f, 1.0f), &b);
        if (b) { b->SetOpacity(op * a); DrawCloud(ccx, ccy, cs, 1.0f, b.Get()); }
    };
    switch (m_weatherType) {
    case WeatherType::Clear: {
        SYSTEMTIME st; GetLocalTime(&st);
        if (st.wHour < 6 || st.wHour >= 18) {
            float mr = r * 0.55f; m_res->whiteBrush->SetOpacity(a * 0.9f);
            ComPtr<ID2D1PathGeometry> pg; fac->CreatePathGeometry(&pg); ComPtr<ID2D1GeometrySink> sk; pg->Open(&sk);
            sk->BeginFigure(D2D1::Point2F(cx - mr * 0.2f, cy - mr), D2D1_FIGURE_BEGIN_FILLED);
            sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - mr * 0.2f, cy + mr), D2D1::SizeF(mr, mr), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - mr * 0.2f, cy - mr), D2D1::SizeF(mr * 0.8f, mr * 0.9f), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close(); ctx->FillGeometry(pg.Get(), m_res->whiteBrush);
        } else {
            float sr = r * 0.55f; D2D1_GRADIENT_STOP stops[2] = { {0.0f, D2D1::ColorF(1, 0.95f, 0.4f, a)}, {1.0f, D2D1::ColorF(1, 0.6f, 0.1f, a)} };
            ComPtr<ID2D1GradientStopCollection> sc; ctx->CreateGradientStopCollection(stops, 2, &sc);
            ComPtr<ID2D1RadialGradientBrush> sb; ctx->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), sr, sr), sc.Get(), &sb);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sr, sr), sb.Get());
            ComPtr<ID2D1SolidColorBrush> rb; ctx->CreateSolidColorBrush(D2D1::ColorF(1, 0.8f, 0.1f, a * 0.6f), &rb);
            for (int i = 0; i < 8; i++) { float ang = t * 0.5f + i * kPi * 0.25f; DrawLine(cx + cosf(ang) * sr * 1.3f, cy + sinf(ang) * sr * 1.3f, cx + cosf(ang) * sr * 1.7f, cy + sinf(ang) * sr * 1.7f, rb.Get(), 1.8f); }
        } break;
    }
    case WeatherType::Rainy:
    case WeatherType::Thunder: {
        bool th = (m_weatherType == WeatherType::Thunder); DC(cx, cy - r * 0.15f, r * 0.85f, 1.0f, th);
        ComPtr<ID2D1SolidColorBrush> rb; ctx->CreateSolidColorBrush(D2D1::ColorF(0.4f, 0.8f, 1.0f, a), &rb);
        for (int i = 0; i < 3; i++) { float p = fmodf(t * 1.8f + i * 0.33f, 1.0f); float dx = cx + (i - 1) * r * 0.4f, dy = cy + r * 0.3f + p * r * 0.6f; rb->SetOpacity(a * (1.0f - p)); DrawLine(dx, dy, dx, dy + r * 0.25f, rb.Get(), 1.5f); }
        if (th && fmodf(t, 2.0f) > 1.8f) {
            ComPtr<ID2D1SolidColorBrush> bb; ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 0, a), &bb); float bx = cx, by = cy + r * 0.2f;
            DrawLine(bx, by, bx + r * 0.2f, by + r * 0.3f, bb.Get(), 2.0f); DrawLine(bx + r * 0.2f, by + r * 0.3f, bx - r * 0.1f, by + r * 0.35f, bb.Get(), 2.0f); DrawLine(bx - r * 0.1f, by + r * 0.35f, bx + r * 0.15f, by + r * 0.6f, bb.Get(), 2.0f);
        } break;
    }
    default: DC(cx, cy, r * 0.9f, 1.0f, false); break;
    }
}
void WeatherComponent::DrawWeatherAmbientBg(float L, float T, float R, float B, float a, ULONGLONG ct) {
    auto* ctx = m_res->d2dContext; auto* fac = m_res->d2dFactory;
    float W = R - L, H = B - T, cx = L + W * 0.5f, t = m_animPhase;
    SYSTEMTIME st; GetLocalTime(&st); float hf = st.wHour + st.wMinute / 60.0f;
    auto ML = [&](D2D1_COLOR_F tc, D2D1_COLOR_F bc) {
        D2D1_GRADIENT_STOP s[2] = { {0.0f, tc}, {1.0f, bc} }; ComPtr<ID2D1GradientStopCollection> sc; ctx->CreateGradientStopCollection(s, 2, &sc);
        ComPtr<ID2D1LinearGradientBrush> b; ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(cx, T), D2D1::Point2F(cx, B)), sc.Get(), &b);
        if (b) b->SetOpacity(a); return b;
    };
    ComPtr<ID2D1RoundedRectangleGeometry> cl; fac->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(L, T, R, B), 16.0f, 16.0f), &cl);
    ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), cl.Get()), nullptr);
    if (m_weatherType == WeatherType::Clear) {
        D2D1_COLOR_F stp, sbt; float sx, sy, sr = W * 0.35f;
        if (hf >= 5.5f && hf < 8.0f) { float p = (hf - 5.5f) / 2.5f; stp = D2D1::ColorF(0.63f, 0.55f, 0.82f); sbt = D2D1::ColorF(0.98f, 0.76f, 0.92f); sx = L + W * 0.1f; sy = B - H * 0.2f * p; }
        else if (hf >= 8.0f && hf < 16.5f) { stp = D2D1::ColorF(0.31f, 0.67f, 1.0f); sbt = D2D1::ColorF(0, 0.95f, 1.0f); sx = cx; sy = T + H * 0.1f; }
        else if (hf >= 16.5f && hf < 19.0f) { float p = (hf - 16.5f) / 2.5f; stp = D2D1::ColorF(1.0f, 0.49f, 0.37f); sbt = D2D1::ColorF(0.99f, 0.71f, 0.48f); sx = R - W * 0.1f; sy = B - H * 0.2f * (1.0f - p); }
        else { stp = D2D1::ColorF(0.06f, 0.13f, 0.15f); sbt = D2D1::ColorF(0.17f, 0.33f, 0.39f); sx = R - W * 0.2f; sy = T + H * 0.2f; }
        auto bg = ML(stp, sbt); if (bg) ctx->FillRectangle(D2D1::RectF(L, T, R, B), bg.Get());
        D2D1_COLOR_F gc = (hf >= 19.0f || hf < 5.5f) ? D2D1::ColorF(1, 1, 1, 0.15f) : D2D1::ColorF(1, 0.9f, 0.6f, 0.3f);
        D2D1_GRADIENT_STOP gs[2] = { {0.0f, gc}, {1.0f, D2D1::ColorF(0,0,0,0)} }; ComPtr<ID2D1GradientStopCollection> gsc; ctx->CreateGradientStopCollection(gs, 2, &gsc);
        ComPtr<ID2D1RadialGradientBrush> gb; ctx->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(sx, sy), D2D1::Point2F(0, 0), sr, sr), gsc.Get(), &gb);
        if (gb) { gb->SetOpacity(a); ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), sr, sr), gb.Get()); }
    } else if (m_weatherType == WeatherType::Rainy || m_weatherType == WeatherType::Thunder) {
        auto bg = ML(D2D1::ColorF(0.17f, 0.25f, 0.38f), D2D1::ColorF(0.07f, 0.06f, 0.05f)); ctx->FillRectangle(D2D1::RectF(L, T, R, B), bg.Get());
        ComPtr<ID2D1SolidColorBrush> rb; ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, a * 0.2f), &rb);
        for (int i = 0; i < 10; i++) { float rx = L + fmodf(i * 77.0f, W), ry = T + fmodf(t * 400.0f + i * 50.0f, H); DrawLine(rx, ry, rx, ry + 15.0f, rb.Get(), 1.0f); }
        if (m_weatherType == WeatherType::Thunder && fmodf(t, 5.0f) > 4.7f) { ComPtr<ID2D1SolidColorBrush> fb; ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, a * 0.3f), &fb); ctx->FillRectangle(D2D1::RectF(L, T, R, B), fb.Get()); }
    } else {
        auto bg = ML(D2D1::ColorF(0.55f, 0.62f, 0.67f), D2D1::ColorF(0.93f, 0.95f, 0.95f)); ctx->FillRectangle(D2D1::RectF(L, T, R, B), bg.Get());
    }
    ctx->PopLayer();
}
void WeatherComponent::DrawWeatherExpanded(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime) {
    if (!m_res) return; auto* ctx = m_res->d2dContext; float L = std::round(rect.left), T = std::round(rect.top), R = std::round(rect.right), B = std::round(rect.bottom);
    float pad = 16.0f, cardW = std::round((R - L - pad * 3) / 2.0f); D2D1_RECT_F leftR = D2D1::RectF(L + pad, T + pad, L + pad + cardW, B - pad); D2D1_RECT_F rightR = D2D1::RectF(R - pad - cardW, T + pad, R - pad, B - pad);
    DrawWeatherAmbientBg(leftR.left, leftR.top, leftR.right, leftR.bottom, contentAlpha, currentTime);
    ComPtr<ID2D1SolidColorBrush> gB, bB; ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.08f * contentAlpha), &gB); ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.24f * contentAlpha), &bB);
    auto DG = [&](const D2D1_RECT_F& r) { D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 22.0f, 22.0f); ctx->FillRoundedRectangle(rr, gB.Get()); ctx->DrawRoundedRectangle(rr, bB.Get(), 1.5f); };
    DG(leftR); DG(rightR);
    m_res->whiteBrush->SetOpacity(contentAlpha); 
    auto locL = GetOrCreateTextLayout(m_locationText.empty() ? L"当前城市" : m_locationText, m_res->titleFormat, cardW - 40.0f, L"exl");
    ctx->DrawTextLayout(D2D1::Point2F(leftR.left + 22, leftR.top + 22), locL.Get(), m_res->whiteBrush);
    m_res->grayBrush->SetOpacity(contentAlpha * 0.85f);
    auto descL = GetOrCreateTextLayout(m_desc, m_res->subFormat, cardW - 40.0f, L"exd");
    ctx->DrawTextLayout(D2D1::Point2F(leftR.left + 22, leftR.top + 48), descL.Get(), m_res->grayBrush);
    std::wstring tstr = std::to_wstring((int)m_temp) + L"\u00B0";
    auto tempL = GetOrCreateTextLayout(tstr, m_res->titleFormat, cardW, L"ext");
    ctx->DrawTextLayout(D2D1::Point2F(leftR.left + 22, leftR.bottom - 60), tempL.Get(), m_res->whiteBrush);
    DrawWeatherIcon(leftR.right - 85, leftR.bottom - 85, 70, contentAlpha, currentTime);
    if (!m_hourly.empty()) {
        float cW = cardW / 3.0f, cH = (rightR.bottom - rightR.top) / 2.0f;
        for (size_t i = 0; i < 6 && i < m_hourly.size(); i++) {
            float cx = rightR.left + (i % 3) * cW, cy = rightR.top + (i / 3) * cH;
            m_res->grayBrush->SetOpacity(contentAlpha * 0.7f);
            auto timeL = GetOrCreateTextLayout(m_hourly[i].time, m_res->subFormat, cW, L"ht"+std::to_wstring(i));
            timeL->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ctx->DrawTextLayout(D2D1::Point2F(cx, cy + 12), timeL.Get(), m_res->grayBrush);
            WeatherType s = m_weatherType; m_weatherType = MapWeatherDescToType(m_hourly[i].text);
            DrawWeatherIcon(cx + cW*0.5f - 12, cy + cH*0.5f - 12, 24, contentAlpha*0.9f, currentTime);
            m_weatherType = s;
            auto htempL = GetOrCreateTextLayout(std::to_wstring((int)m_hourly[i].temp) + L"\u00B0", m_res->subFormat, cW, L"htp"+std::to_wstring(i));
            htempL->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_res->whiteBrush->SetOpacity(contentAlpha * 0.95f);
            ctx->DrawTextLayout(D2D1::Point2F(cx, cy + cH - 25), htempL.Get(), m_res->whiteBrush);
        }
    }
}
void WeatherComponent::DrawWeatherDaily(const D2D1_RECT_F& r, float a, ULONGLONG t) {
    if (!m_res) return; auto* ctx = m_res->d2dContext; float L = std::round(r.left), T = std::round(r.top), R = std::round(r.right), B = std::round(r.bottom);
    float pad = 16.0f; ComPtr<ID2D1SolidColorBrush> gB; ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.06f * a), &gB); ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(L + pad, T + pad, R - pad, B - pad), 22, 22), gB.Get());
    if (m_daily.empty()) return;
    float cellW = (R - L - pad * 2) / 7.0f;
    for (size_t i = 0; i < 7 && i < m_daily.size(); i++) {
        float cx = L + pad + i * cellW + cellW * 0.5f;
        m_res->grayBrush->SetOpacity(a * 0.7f);
        auto dateL = GetOrCreateTextLayout(m_daily[i].date, m_res->subFormat, cellW, L"dd"+std::to_wstring(i));
        dateL->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(cx - cellW * 0.5f, T + pad + 15.0f), dateL.Get(), m_res->grayBrush);
        WeatherType saved = m_weatherType; m_weatherType = MapWeatherDescToType(m_daily[i].textDay);
        DrawWeatherIcon(cx - 14.0f, T + pad + 35.0f, 28.0f, a, t);
        m_weatherType = saved;
        auto tempL = GetOrCreateTextLayout(std::to_wstring((int)m_daily[i].tempMax)+L"\u00B0", m_res->subFormat, cellW, L"dt"+std::to_wstring(i));
        tempL->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_res->whiteBrush->SetOpacity(a);
        ctx->DrawTextLayout(D2D1::Point2F(cx - cellW * 0.5f, T + pad + 70.0f), tempL.Get(), m_res->whiteBrush);
    }
}
void WeatherComponent::DrawUnavailable(const D2D1_RECT_F& r, float a, ULONGLONG t) {
    auto* ctx = m_res->d2dContext; ComPtr<ID2D1SolidColorBrush> b; ctx->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.4f * a), &b); ctx->FillRoundedRectangle(D2D1::RoundedRect(r, 20, 20), b.Get());
}
void WeatherComponent::DrawCloud(float ccx, float ccy, float s, float op, ID2D1Brush* brush) {
    auto* ctx = m_res->d2dContext; float oldO = brush->GetOpacity(); brush->SetOpacity(oldO * op);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx, ccy), s * 1.0f, s * 0.45f), brush); ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx - s * 0.5f, ccy - s * 0.25f), s * 0.45f, s * 0.4f), brush);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx + s * 0.45f, ccy - s * 0.2f), s * 0.4f, s * 0.35f), brush); ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx - s * 0.1f, ccy - s * 0.45f), s * 0.42f, s * 0.38f), brush);
    brush->SetOpacity(oldO);
}
void WeatherComponent::DrawLine(float x1, float y1, float x2, float y2, ID2D1Brush* b, float w) {
    ComPtr<ID2D1StrokeStyle> ss; m_res->d2dFactory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &ss);
    m_res->d2dContext->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, w, ss.Get());
}
WeatherType WeatherComponent::MapWeatherDescToType(const std::wstring& d) const {
    std::wstring s = d; for (auto& c : s) c = towlower(c);
    if (s.find(L"雪") != std::wstring::npos) return WeatherType::Snow; if (s.find(L"雷") != std::wstring::npos) return WeatherType::Thunder; if (s.find(L"雨") != std::wstring::npos) return WeatherType::Rainy;
    if (s.find(L"云") != std::wstring::npos || s.find(L"阴") != std::wstring::npos) return WeatherType::Cloudy; if (s.find(L"晴") != std::wstring::npos) return WeatherType::Clear;
    return WeatherType::Default;
}
ComPtr<IDWriteTextLayout> WeatherComponent::GetOrCreateTextLayout(const std::wstring& t, IDWriteTextFormat* f, float mw, const std::wstring& ck) {
    auto it = m_layoutCache.find(ck); if (it != m_layoutCache.end() && it->second.text == t && it->second.maxWidth == mw) return it->second.layout;
    ComPtr<IDWriteTextLayout> l; m_res->dwriteFactory->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, mw, 1000.0f, &l);
    m_layoutCache[ck] = { l, t, mw }; return l;
}
