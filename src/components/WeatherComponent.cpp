#include "components/WeatherComponent.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

// ── 生命周期 ───────────────────────────────────────────────────────────────

void WeatherComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void WeatherComponent::Update(float deltaTime) {
    // animPhase 在 Draw 里按 currentTime 推进，此处保留供未来使用
}

// ── 数据更新 ───────────────────────────────────────────────────────────────

void WeatherComponent::SetWeatherData(
    const std::wstring& desc, float temp, const std::wstring& iconId,
    const std::vector<RenderContext::HourlyForecast>& hourly,
    const std::vector<RenderContext::DailyForecast>& daily)
{
    WeatherType newType = MapWeatherDescToType(desc);
    if (newType != m_weatherType) {
        m_weatherType = newType;
        ResetAnimation();
    }
    m_desc   = desc;
    m_temp   = temp;
    m_iconId = iconId;
    m_hourly = hourly;
    m_daily  = daily;
}

void WeatherComponent::SetExpanded(bool expanded) {
    if (expanded && !m_isExpanded) ResetAnimation();
    m_isExpanded = expanded;
}

// ── 主绘制入口 ─────────────────────────────────────────────────────────────

void WeatherComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res || !m_isExpanded) return;

    if (m_viewMode == WeatherViewMode::Daily)
        DrawWeatherDaily(rect, contentAlpha, currentTimeMs);
    else
        DrawWeatherExpanded(rect, contentAlpha, currentTimeMs);
}

void WeatherComponent::DrawCompact(float iconX, float iconY, float iconSize,
                                   float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res) return;
    // 推进动画 phase（compact 路径）
    if (m_lastAnimTime == 0) m_lastAnimTime = currentTimeMs;
    float dt = (float)(currentTimeMs - m_lastAnimTime) / 1000.0f;
    if (dt > 0.0f && dt < 0.5f) m_animPhase += dt * 2.0f;
    m_lastAnimTime = currentTimeMs;

    DrawWeatherIcon(iconX, iconY, iconSize, contentAlpha, currentTimeMs);
}

bool WeatherComponent::OnMouseWheel(float /*x*/, float /*y*/, int delta) {
    if (!m_isExpanded) return false;
    if (delta < 0)
        m_viewMode = WeatherViewMode::Daily;
    else
        m_viewMode = WeatherViewMode::Hourly;
    return true;
}

// ── TextLayout 缓存 ────────────────────────────────────────────────────────

ComPtr<IDWriteTextLayout> WeatherComponent::GetOrCreateTextLayout(
    const std::wstring& text, IDWriteTextFormat* fmt,
    float maxWidth, const std::wstring& cacheKey)
{
    auto it = m_layoutCache.find(cacheKey);
    if (it != m_layoutCache.end() && it->second.text == text && it->second.maxWidth == maxWidth)
        return it->second.layout;

    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, maxWidth, 10000.0f, &layout);
    m_layoutCache[cacheKey] = { layout, text, maxWidth };
    return layout;
}

// ── 辅助绘制 ──────────────────────────────────────────────────────────────

void WeatherComponent::DrawCloud(float ccx, float ccy, float s, float op, ID2D1Brush* brush) {
    brush->SetOpacity(op * 1.0f); // alpha 由调用方通过 SetOpacity 管理
    auto* ctx = m_res->d2dContext;
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx,           ccy),          s*1.00f, s*0.44f), brush);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx-s*0.52f,   ccy-s*0.22f),  s*0.42f, s*0.38f), brush);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx+s*0.42f,   ccy-s*0.17f),  s*0.38f, s*0.33f), brush);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx-s*0.15f,   ccy-s*0.42f),  s*0.40f, s*0.36f), brush);
    brush->SetOpacity(1.0f);
}

void WeatherComponent::DrawLine(float x1, float y1, float x2, float y2, ID2D1Brush* brush, float w) {
    auto* ctx  = m_res->d2dContext;
    auto* fac  = m_res->d2dFactory;
    ComPtr<ID2D1PathGeometry> pg; fac->CreatePathGeometry(&pg);
    ComPtr<ID2D1GeometrySink> sk; pg->Open(&sk);
    sk->BeginFigure(D2D1::Point2F(x1, y1), D2D1_FIGURE_BEGIN_HOLLOW);
    sk->AddLine(D2D1::Point2F(x2, y2));
    sk->EndFigure(D2D1_FIGURE_END_OPEN); sk->Close();
    ComPtr<ID2D1StrokeStyle> ss;
    fac->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &ss);
    ctx->DrawGeometry(pg.Get(), brush, w, ss.Get());
}

// ── MapWeatherDescToType ───────────────────────────────────────────────────

WeatherType WeatherComponent::MapWeatherDescToType(const std::wstring& desc) const {
    std::wstring d = desc;
    for (auto& c : d) c = towlower(c);
    if (d.find(L'雪') != std::wstring::npos || d.find(L'霜') != std::wstring::npos) return WeatherType::Snow;
    if (d.find(L'雾') != std::wstring::npos || d.find(L'霾') != std::wstring::npos) return WeatherType::Fog;
    if (d.find(L'雷暴') != std::wstring::npos || d.find(L'雷电') != std::wstring::npos) return WeatherType::Thunder;
    if (d.find(L'雨') != std::wstring::npos || d.find(L'阵雨') != std::wstring::npos) return WeatherType::Rainy;
    if (d.find(L'晴') != std::wstring::npos && d.find(L'多云') == std::wstring::npos && d.find(L'阴') == std::wstring::npos) return WeatherType::Clear;
    if (d.find(L'多云') != std::wstring::npos || d.find(L'间晴') != std::wstring::npos || d.find(L'间多云') != std::wstring::npos) return WeatherType::PartlyCloudy;
    if (d.find(L'阴') != std::wstring::npos) return WeatherType::Cloudy;
    return WeatherType::Default;
}

// ── DrawWeatherIcon ────────────────────────────────────────────────────────

void WeatherComponent::DrawWeatherIcon(float x, float y, float size, float alpha, ULONGLONG currentTime) {
    if (!m_res) return;
    auto* ctx = m_res->d2dContext;
    auto* fac = m_res->d2dFactory;
    auto* wb  = m_res->whiteBrush;
    wb->SetOpacity(alpha);

    float cx = x + size * 0.5f;
    float cy = y + size * 0.5f;
    float r  = size * 0.45f;
    float t  = m_animPhase;

    switch (m_weatherType) {
    case WeatherType::Clear: {
        SYSTEMTIME st; GetLocalTime(&st);
        bool isNight = (st.wHour < 6 || st.wHour >= 18);
        if (isNight) {
            float moonR = r * 0.55f, moonX = cx - r*0.1f, moonY = cy - r*0.05f;
            float holeOffsetX = r*0.35f, holeR = moonR*0.85f;
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(moonX, moonY), moonR, moonR), wb);
            ComPtr<ID2D1SolidColorBrush> darkBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E, 1.0f), &darkBrush);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(moonX+holeOffsetX, moonY-r*0.05f), holeR, holeR), darkBrush.Get());

            struct Star { float dx, dy, r; };
            Star stars[4] = { {-r*1.2f,-r*0.8f,r*0.12f},{r*0.9f,-r*1.0f,r*0.10f},{r*1.1f,r*0.5f,r*0.08f},{-r*0.8f,r*0.9f,r*0.09f} };
            float twinkleDur = 1.5f * 3.14159265f;
            float displayPhase = (m_animPhase < twinkleDur) ? m_animPhase : twinkleDur;
            for (int i = 0; i < 4; i++) {
                float sx = cx + stars[i].dx, sy = cy + stars[i].dy, sr = stars[i].r;
                float twinkle = sinf(displayPhase * (1.0f + i*0.3f));
                ComPtr<ID2D1PathGeometry> sg; fac->CreatePathGeometry(&sg);
                ComPtr<ID2D1GeometrySink> ssk; sg->Open(&ssk);
                float astep = 3.14159265f / 4.0f;
                ssk->BeginFigure(D2D1::Point2F(sx, sy), D2D1_FIGURE_BEGIN_FILLED);
                for (int p = 0; p < 8; p++) {
                    float angle = p * astep;
                    float len = (p % 2 == 0) ? sr : sr*0.3f;
                    ssk->AddLine(D2D1::Point2F(sx + cosf(angle)*len, sy + sinf(angle)*len));
                }
                ssk->EndFigure(D2D1_FIGURE_END_CLOSED); ssk->Close();
                float starAlpha = (0.3f + 0.7f*fabsf(twinkle)) * alpha;
                wb->SetOpacity(starAlpha);
                ctx->FillGeometry(sg.Get(), wb);
            }
        } else {
            // 太阳
            float sunR = r * 0.52f;
            ComPtr<ID2D1SolidColorBrush> sunBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.90f, 0.30f, alpha), &sunBrush);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sunR, sunR), sunBrush.Get());
            sunBrush->SetOpacity(0.75f * alpha);
            float rayOff = sunR * 1.35f, rayLen = sunR * 0.50f;
            for (int i = 0; i < 8; i++) {
                float a = t*0.55f + i*3.14159f*0.25f;
                DrawLine(cx+cosf(a)*rayOff, cy+sinf(a)*rayOff,
                         cx+cosf(a)*(rayOff+rayLen), cy+sinf(a)*(rayOff+rayLen),
                         sunBrush.Get(), 1.4f);
            }
        }
        break;
    }
    case WeatherType::Rainy:
    case WeatherType::Thunder: {
        bool isThunder = (m_weatherType == WeatherType::Thunder);
        // 云体
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(isThunder ? D2D1::ColorF(0.30f,0.32f,0.40f,alpha) : D2D1::ColorF(0.60f,0.62f,0.68f,alpha), &cloudBrush);
        DrawCloud(cx, cy - r*0.10f, r*0.80f, 1.0f, cloudBrush.Get());
        // 雨滴
        ComPtr<ID2D1SolidColorBrush> rainBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.55f,0.75f,1.0f, 0.90f*alpha), &rainBrush);
        int dropN = isThunder ? 5 : 3;
        for (int i = 0; i < dropN; i++) {
            float phase = fmodf(t*2.2f + i*0.4f, 1.0f);
            float dx = cx + (i-(dropN/2))*r*0.35f;
            float dy = cy + r*0.40f + phase*r*0.55f;
            rainBrush->SetOpacity((0.5f + 0.5f*(1.0f-phase)) * alpha);
            DrawLine(dx, dy, dx+r*0.03f, dy+r*0.22f, rainBrush.Get(), 1.0f);
        }
        if (isThunder) {
            // 闪电
            ComPtr<ID2D1SolidColorBrush> boltBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.95f,0.40f, alpha), &boltBrush);
            float bx = cx + r*0.10f, by = cy + r*0.10f;
            DrawLine(bx, by, bx+r*0.22f, by+r*0.28f, boltBrush.Get(), 1.5f);
            DrawLine(bx+r*0.22f, by+r*0.28f, bx+r*0.04f, by+r*0.28f, boltBrush.Get(), 1.5f);
            DrawLine(bx+r*0.04f, by+r*0.28f, bx+r*0.28f, by+r*0.58f, boltBrush.Get(), 1.5f);
        }
        break;
    }
    case WeatherType::Snow: {
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.75f,0.78f,0.84f,alpha), &cloudBrush);
        DrawCloud(cx, cy - r*0.12f, r*0.78f, 0.9f, cloudBrush.Get());
        ComPtr<ID2D1SolidColorBrush> snowBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f), &snowBrush);
        for (int i = 0; i < 3; i++) {
            float phase = fmodf(t*0.55f + i*0.35f, 1.0f);
            float fx = cx + (i-1)*r*0.40f + sinf(t*0.5f+i)*r*0.08f;
            float fy = cy + r*0.38f + phase*r*0.52f;
            float fs = r*0.14f;
            snowBrush->SetOpacity((0.5f+0.5f*sinf(phase*3.14159f)) * alpha);
            for (int j = 0; j < 6; j++) {
                float a = t*1.1f + j*3.14159f/3.0f + i*0.7f;
                DrawLine(fx, fy, fx+cosf(a)*fs, fy+sinf(a)*fs, snowBrush.Get(), 0.8f);
            }
        }
        break;
    }
    case WeatherType::Fog: {
        ComPtr<ID2D1SolidColorBrush> fogBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.75f,0.75f,0.75f), &fogBrush);
        for (int i = 0; i < 3; i++) {
            float fy = y + size*(0.28f + i*0.22f);
            float drift = sinf(t*0.25f + i*1.2f)*size*0.06f;
            float fw = size*(0.78f - i*0.08f);
            fogBrush->SetOpacity((0.55f - i*0.10f)*alpha);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx+drift, fy), fw*0.5f, size*0.07f), fogBrush.Get());
        }
        break;
    }
    case WeatherType::PartlyCloudy: {
        // 小太阳
        ComPtr<ID2D1SolidColorBrush> sunBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.88f,0.25f,alpha), &sunBrush);
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx+r*0.20f, cy-r*0.18f), r*0.38f, r*0.38f), sunBrush.Get());
        // 云遮住部分太阳
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.90f,0.92f,0.95f,alpha), &cloudBrush);
        DrawCloud(cx - r*0.10f, cy + r*0.05f, r*0.68f, 1.0f, cloudBrush.Get());
        break;
    }
    case WeatherType::Cloudy:
    case WeatherType::Default:
    default: {
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.72f,0.74f,0.80f,alpha), &cloudBrush);
        DrawCloud(cx, cy, r*0.85f, 1.0f, cloudBrush.Get());
        break;
    }
    }

    wb->SetOpacity(alpha);
}

// ── DrawWeatherAmbientBg ───────────────────────────────────────────────────

void WeatherComponent::DrawWeatherAmbientBg(float L, float T, float R, float B, float alpha, ULONGLONG currentTime) {
    if (m_lastAnimTime == 0) m_lastAnimTime = currentTime;
    float dt = (float)(currentTime - m_lastAnimTime) / 1000.0f;
    if (dt > 0.0f && dt < 0.5f) m_animPhase += dt * 1.5f;
    m_lastAnimTime = currentTime;

    auto* ctx = m_res->d2dContext;
    auto* fac = m_res->d2dFactory;
    float W = R - L, H = B - T;
    float cx = L + W * 0.5f;
    float t  = m_animPhase;

    auto MakeLinGrad = [&](D2D1_COLOR_F topC, D2D1_COLOR_F botC) -> ComPtr<ID2D1LinearGradientBrush> {
        D2D1_GRADIENT_STOP stops[2] = { {0.0f, topC}, {1.0f, botC} };
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 2, &coll))) return nullptr;
        ComPtr<ID2D1LinearGradientBrush> b;
        ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(cx,T), D2D1::Point2F(cx,B)), coll.Get(), &b);
        if (b) b->SetOpacity(alpha);
        return b;
    };
    auto MakeRadGrad = [&](float ox, float oy, float rx, float ry, D2D1_COLOR_F inner, D2D1_COLOR_F outer) -> ComPtr<ID2D1RadialGradientBrush> {
        D2D1_GRADIENT_STOP stops[2] = { {0.0f, inner}, {1.0f, outer} };
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 2, &coll))) return nullptr;
        ComPtr<ID2D1RadialGradientBrush> b;
        ctx->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(ox,oy), D2D1::Point2F(0,0), rx, ry), coll.Get(), &b);
        if (b) b->SetOpacity(alpha);
        return b;
    };
    auto DrawCloudLocal = [&](float ccx, float ccy, float s, float op, ID2D1Brush* brush) {
        brush->SetOpacity(op * alpha);
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx,         ccy),         s*1.00f, s*0.44f), brush);
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx-s*0.52f, ccy-s*0.22f), s*0.42f, s*0.38f), brush);
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx+s*0.42f, ccy-s*0.17f), s*0.38f, s*0.33f), brush);
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ccx-s*0.15f, ccy-s*0.42f), s*0.40f, s*0.36f), brush);
        brush->SetOpacity(alpha);
    };
    auto DrawLineLocal = [&](float x1, float y1, float x2, float y2, ID2D1Brush* brush, float w) {
        ComPtr<ID2D1PathGeometry> pg; fac->CreatePathGeometry(&pg);
        ComPtr<ID2D1GeometrySink> sk; pg->Open(&sk);
        sk->BeginFigure(D2D1::Point2F(x1,y1), D2D1_FIGURE_BEGIN_HOLLOW);
        sk->AddLine(D2D1::Point2F(x2,y2));
        sk->EndFigure(D2D1_FIGURE_END_OPEN); sk->Close();
        ComPtr<ID2D1StrokeStyle> ss;
        fac->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &ss);
        ctx->DrawGeometry(pg.Get(), brush, w, ss.Get());
    };

    // 裁剪
    ComPtr<ID2D1RoundedRectangleGeometry> clipGeo;
    fac->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(L,T,R,B), 16.0f, 16.0f), &clipGeo);
    ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo.Get()), nullptr);

    switch (m_weatherType) {
    case WeatherType::Rainy:
    case WeatherType::Thunder: {
        bool isThunder = (m_weatherType == WeatherType::Thunder);
        auto bg = isThunder
            ? MakeLinGrad(D2D1::ColorF(0.07f,0.08f,0.14f), D2D1::ColorF(0.11f,0.13f,0.21f))
            : MakeLinGrad(D2D1::ColorF(0.13f,0.17f,0.27f), D2D1::ColorF(0.18f,0.23f,0.36f));
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());

        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(isThunder ? D2D1::ColorF(0.30f,0.31f,0.38f) : D2D1::ColorF(0.42f,0.45f,0.52f), &cloudBrush);
        float d1=sinf(t*0.28f)*W*0.04f, d2=sinf(t*0.19f+1.0f)*W*0.03f, d3=sinf(t*0.23f+2.1f)*W*0.035f;
        DrawCloudLocal(cx+d1,         T+H*0.24f, W*0.34f, 0.90f, cloudBrush.Get());
        DrawCloudLocal(L+W*0.22f+d2,  T+H*0.19f, W*0.24f, 0.72f, cloudBrush.Get());
        DrawCloudLocal(R-W*0.18f+d3,  T+H*0.26f, W*0.22f, 0.66f, cloudBrush.Get());

        ComPtr<ID2D1SolidColorBrush> rainBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.62f,0.74f,0.92f,0.75f), &rainBrush);
        rainBrush->SetOpacity(0.75f * alpha);
        int dropN = isThunder ? 22 : 15;
        float speed = isThunder ? 3.8f : 2.4f;
        float dropH = H*0.13f, angleSlant = 0.14f;
        for (int i = 0; i < dropN; i++) {
            float xBase = L + fmodf(i*W*0.618f, W);
            float phase = fmodf(t*speed + i*0.41f, 1.0f);
            float dy = T + H*0.42f + phase*H*0.62f;
            float dx = xBase + phase*dropH*angleSlant;
            dx = L + fmodf(dx-L, W);
            DrawLineLocal(dx, dy, dx+dropH*angleSlant, dy+dropH, rainBrush.Get(), isThunder ? 1.1f : 0.85f);
        }
        if (isThunder) {
            float fp = fmodf(t/2.8f, 1.0f);
            if (fp < 0.10f) {
                float fa = (1.0f - fp/0.10f)*0.55f*alpha;
                ComPtr<ID2D1SolidColorBrush> flashBg;
                ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,0.88f,fa), &flashBg);
                ctx->FillRectangle(D2D1::RectF(L,T,R,B), flashBg.Get());
                float bx=cx+W*0.05f, by=T+H*0.34f;
                ComPtr<ID2D1SolidColorBrush> boltBrush;
                ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.95f,0.45f,alpha), &boltBrush);
                ComPtr<ID2D1PathGeometry> bolt; fac->CreatePathGeometry(&bolt);
                ComPtr<ID2D1GeometrySink> bsk; bolt->Open(&bsk);
                bsk->BeginFigure(D2D1::Point2F(bx,by), D2D1_FIGURE_BEGIN_HOLLOW);
                bsk->AddLine(D2D1::Point2F(bx+W*0.07f, by+H*0.20f));
                bsk->AddLine(D2D1::Point2F(bx-W*0.03f, by+H*0.20f));
                bsk->AddLine(D2D1::Point2F(bx+W*0.11f, by+H*0.48f));
                bsk->EndFigure(D2D1_FIGURE_END_OPEN); bsk->Close();
                ComPtr<ID2D1StrokeStyle> bss;
                fac->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND), nullptr, 0, &bss);
                ctx->DrawGeometry(bolt.Get(), boltBrush.Get(), 2.8f, bss.Get());
            }
        }
        break;
    }
    case WeatherType::Snow: {
        auto bg = MakeLinGrad(D2D1::ColorF(0.11f,0.15f,0.25f), D2D1::ColorF(0.19f,0.24f,0.38f));
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.60f,0.64f,0.72f), &cloudBrush);
        DrawCloudLocal(cx+sinf(t*0.18f)*W*0.03f,        T+H*0.21f, W*0.30f, 0.72f, cloudBrush.Get());
        DrawCloudLocal(L+W*0.20f+sinf(t*0.14f)*W*0.02f, T+H*0.17f, W*0.20f, 0.52f, cloudBrush.Get());
        DrawCloudLocal(R-W*0.15f+sinf(t*0.21f)*W*0.02f, T+H*0.23f, W*0.18f, 0.50f, cloudBrush.Get());
        ComPtr<ID2D1SolidColorBrush> snowBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f), &snowBrush);
        for (int i = 0; i < 14; i++) {
            float phase = fmodf(t*0.55f + i*0.21f, 1.0f);
            float fx = L + fmodf(i*W*0.13f + sinf(t*0.35f+i)*W*0.06f, W);
            float fy = T + H*0.35f + phase*H*0.68f;
            float fs = W*0.022f + (i%3)*W*0.007f;
            float fa = 0.45f + 0.50f*sinf(phase*3.14159f);
            snowBrush->SetOpacity(fa * alpha);
            float fAngle = t*1.1f + i*2.094f;
            for (int j = 0; j < 6; j++) {
                float a = fAngle + j*3.14159f/3.0f;
                DrawLineLocal(fx, fy, fx+cosf(a)*fs, fy+sinf(a)*fs, snowBrush.Get(), 0.9f);
                float mx=fx+cosf(a)*fs*0.55f, my=fy+sinf(a)*fs*0.55f;
                float ba = a + 3.14159f*0.5f;
                DrawLineLocal(mx-cosf(ba)*fs*0.22f, my-sinf(ba)*fs*0.22f, mx+cosf(ba)*fs*0.22f, my+sinf(ba)*fs*0.22f, snowBrush.Get(), 0.7f);
            }
        }
        snowBrush->SetOpacity(alpha);
        break;
    }
    case WeatherType::Clear: {
        SYSTEMTIME st; GetLocalTime(&st);
        bool isNight = (st.wHour < 6 || st.wHour >= 18);
        if (isNight) {
            auto bg = MakeLinGrad(D2D1::ColorF(0.03f,0.03f,0.12f), D2D1::ColorF(0.06f,0.08f,0.20f));
            if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
            ComPtr<ID2D1SolidColorBrush> starBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f), &starBrush);
            struct SP { float rx,ry,sz; } stars[] = {
                {0.10f,0.14f,1.3f},{0.24f,0.07f,1.0f},{0.44f,0.20f,1.5f},{0.63f,0.09f,1.1f},
                {0.78f,0.24f,1.4f},{0.34f,0.32f,0.9f},{0.70f,0.38f,1.1f},{0.14f,0.42f,1.2f},
                {0.54f,0.48f,0.8f},{0.88f,0.14f,1.2f},{0.50f,0.14f,1.0f},{0.82f,0.42f,0.9f}
            };
            for (auto& s : stars) {
                float tw = 0.35f + 0.65f*fabsf(sinf(t*(0.7f+s.sz*0.2f)+s.rx*7.0f));
                starBrush->SetOpacity(tw * alpha);
                ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(L+s.rx*W, T+s.ry*H), s.sz, s.sz), starBrush.Get());
            }
            float moonX=R-W*0.24f, moonY=T+H*0.24f, moonR=W*0.13f;
            ComPtr<ID2D1SolidColorBrush> moonBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.95f,0.82f,alpha), &moonBrush);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(moonX,moonY), moonR, moonR), moonBrush.Get());
            ComPtr<ID2D1SolidColorBrush> holeBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(0.04f,0.05f,0.16f,alpha), &holeBrush);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(moonX+moonR*0.48f, moonY-moonR*0.08f), moonR*0.84f, moonR*0.84f), holeBrush.Get());
        } else {
            auto bg = MakeLinGrad(D2D1::ColorF(0.22f,0.52f,0.90f), D2D1::ColorF(0.52f,0.80f,1.0f));
            if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
            float sunX=R-W*0.22f, sunY=T+H*0.24f, sunR=W*0.14f;
            auto glow = MakeRadGrad(sunX,sunY,sunR*2.4f,sunR*2.4f, D2D1::ColorF(1.0f,0.92f,0.40f,0.30f*alpha), D2D1::ColorF(1.0f,0.92f,0.40f,0.0f));
            if (glow) ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sunX,sunY), sunR*2.4f, sunR*2.4f), glow.Get());
            ComPtr<ID2D1SolidColorBrush> sunBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.93f,0.44f,alpha), &sunBrush);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sunX,sunY), sunR, sunR), sunBrush.Get());
            sunBrush->SetOpacity(0.70f*alpha);
            float rayOff=sunR*1.28f, rayLen=sunR*0.46f;
            for (int i = 0; i < 8; i++) {
                float a = t*0.38f + i*3.14159f*0.25f;
                DrawLineLocal(sunX+cosf(a)*rayOff, sunY+sinf(a)*rayOff, sunX+cosf(a)*(rayOff+rayLen), sunY+sinf(a)*(rayOff+rayLen), sunBrush.Get(), 2.0f);
            }
            ComPtr<ID2D1SolidColorBrush> cloudBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f), &cloudBrush);
            DrawCloudLocal(L+W*0.38f+sinf(t*0.22f)*W*0.03f, T+H*0.60f, W*0.22f, 0.60f, cloudBrush.Get());
        }
        break;
    }
    case WeatherType::PartlyCloudy:
    case WeatherType::Cloudy:
    case WeatherType::Default: {
        bool partlyCloudy = (m_weatherType == WeatherType::PartlyCloudy);
        SYSTEMTIME st; GetLocalTime(&st);
        bool isNight = (st.wHour < 6 || st.wHour >= 18);
        float maxCov=partlyCloudy?0.55f:0.90f, entryDur=3.0f;
        float rawT = fminf(t/entryDur, 1.0f);
        float cover = maxCov * rawT*rawT*(3.0f-2.0f*rawT);
        auto Lerp = [](float a, float b, float f) { return a+(b-a)*f; };
        float skyR,skyG,skyB,ovR,ovG,ovB;
        if (isNight) { skyR=0.04f;skyG=0.05f;skyB=0.16f; ovR=0.10f;ovG=0.11f;ovB=0.20f; }
        else         { skyR=0.24f;skyG=0.54f;skyB=0.92f; ovR=0.36f;ovG=0.38f;ovB=0.44f; }
        D2D1_COLOR_F bgTop = D2D1::ColorF(Lerp(skyR,ovR,cover), Lerp(skyG,ovG,cover), Lerp(skyB,ovB,cover));
        D2D1_COLOR_F bgBot = isNight
            ? D2D1::ColorF(Lerp(skyR+0.02f,ovR+0.03f,cover), Lerp(skyG+0.03f,ovG+0.03f,cover), Lerp(skyB+0.08f,ovB+0.05f,cover))
            : D2D1::ColorF(Lerp(skyR+0.12f,ovR+0.08f,cover), Lerp(skyG+0.16f,ovG+0.06f,cover), Lerp(skyB+0.06f,ovB+0.02f,cover));
        auto bg = MakeLinGrad(bgTop, bgBot);
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        if (!isNight) {
            float sunVis = fmaxf(0.0f, 1.0f - cover/0.5f);
            if (sunVis > 0.01f) {
                float sunX=R-W*0.22f, sunY=T+H*0.23f, sunR=W*0.12f;
                auto glow = MakeRadGrad(sunX,sunY,sunR*2.3f,sunR*2.3f,
                    D2D1::ColorF(1.0f,0.92f,0.38f,0.28f*alpha*sunVis), D2D1::ColorF(1.0f,0.92f,0.38f,0.0f));
                if (glow) ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sunX,sunY), sunR*2.3f, sunR*2.3f), glow.Get());
                ComPtr<ID2D1SolidColorBrush> sunBrush;
                ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.93f,0.44f,alpha*sunVis), &sunBrush);
                ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sunX,sunY), sunR, sunR), sunBrush.Get());
            }
        }
        struct CloudDef { float relX,relY,scale,entryDelay,driftSpd,driftAmp; };
        CloudDef clouds[] = {
            {0.50f,0.20f,0.36f,0.00f,0.22f,0.030f},
            {0.18f,0.26f,0.28f,0.20f,0.17f,0.024f},
            {0.82f,0.17f,0.26f,0.35f,0.25f,0.026f},
            {0.40f,0.44f,0.30f,0.50f,0.14f,0.020f},
            {0.70f,0.40f,0.24f,0.65f,0.19f,0.022f},
        };
        int numClouds = partlyCloudy ? 3 : 5;
        D2D1_COLOR_F cloudCol = isNight ? D2D1::ColorF(0.32f,0.34f,0.44f) : D2D1::ColorF(0.88f,0.90f,0.95f);
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(cloudCol, &cloudBrush);
        for (int i = 0; i < numClouds; i++) {
            auto& c = clouds[i];
            float cloudRaw = fminf(fmaxf(t/entryDur-c.entryDelay, 0.0f)/(1.0f-c.entryDelay), 1.0f);
            float arriveT  = cloudRaw*cloudRaw*(3.0f-2.0f*cloudRaw);
            float targetX  = L + c.relX*W;
            float startX   = R + c.scale*W;
            float baseX    = startX + (targetX-startX)*arriveT;
            float windDrift = sinf(t*c.driftSpd + i*2.094f)*W*c.driftAmp;
            float cloudX   = baseX + windDrift;
            float cloudY   = T + c.relY*H + sinf(t*c.driftSpd*0.7f+i*1.3f)*H*0.012f;
            float cloudOp  = (c.scale > 0.30f ? 0.92f : 0.80f) * arriveT;
            DrawCloudLocal(cloudX, cloudY, c.scale*W, cloudOp, cloudBrush.Get());
        }
        break;
    }
    case WeatherType::Fog: {
        auto bg = MakeLinGrad(D2D1::ColorF(0.58f,0.59f,0.61f), D2D1::ColorF(0.70f,0.71f,0.73f));
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        ComPtr<ID2D1SolidColorBrush> fogBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f), &fogBrush);
        for (int i = 0; i < 5; i++) {
            float fy=T+H*(0.18f+i*0.16f), drift=sinf(t*0.20f+i*1.2f)*W*0.06f;
            float fw=W*(0.88f-i*0.06f), fo=(0.30f-i*0.04f)*alpha;
            fogBrush->SetOpacity(fo);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx+drift, fy), fw*0.5f, H*0.055f), fogBrush.Get());
        }
        fogBrush->SetOpacity(alpha);
        break;
    }
    }

    ctx->PopLayer();
}

// ── DrawWeatherDaily ───────────────────────────────────────────────────────

void WeatherComponent::DrawWeatherDaily(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime) {
    if (!m_res) return;
    auto* ctx = m_res->d2dContext;
    float left=rect.left, top=rect.top, right=rect.right, bottom=rect.bottom;
    float islandWidth = right - left, islandHeight = bottom - top;
    float padding = 15.0f;
    float cardTop = top + padding, cardBottom = bottom - padding;

    ComPtr<ID2D1SolidColorBrush> glassBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.15f,0.15f,0.15f,0.4f*contentAlpha), &glassBrush);
    D2D1_ROUNDED_RECT bgCard = D2D1::RoundedRect(D2D1::RectF(left+padding, cardTop, right-padding, cardBottom), 16.0f, 16.0f);
    ctx->FillRoundedRectangle(&bgCard, glassBrush.Get());

    std::wstring titleText = L"未来天气";
    ComPtr<IDWriteTextFormat> titleFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-cn", &titleFmt);
    m_res->grayBrush->SetOpacity(contentAlpha * 0.7f);
    ctx->DrawTextW(titleText.c_str(), (UINT32)titleText.size(), titleFmt.Get(),
        D2D1::RectF(left+padding+10.0f, cardTop+4.0f, right-padding, cardTop+20.0f), m_res->grayBrush);

    if (m_daily.empty()) {
        std::wstring emptyText = L"暂无预报数据";
        m_res->grayBrush->SetOpacity(contentAlpha);
        auto emptyLayout = GetOrCreateTextLayout(emptyText, m_res->subFormat, islandWidth, L"df_empty");
        emptyLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(left+padding, cardTop+(cardBottom-cardTop)/2.0f-8.0f), emptyLayout.Get(), m_res->grayBrush);
        return;
    }

    size_t count = (std::min)(m_daily.size(), (size_t)7);
    float totalW = (right-padding)-(left+padding)-20.0f;
    float cellW = totalW / (float)count;
    float dataTop = cardTop + 22.0f, dataBottom = cardBottom - 6.0f;

    // 推进动画 phase（逐日视图路径）
    if (m_lastAnimTime == 0) m_lastAnimTime = currentTime;
    float dtDaily = (float)(currentTime - m_lastAnimTime) / 1000.0f;
    if (dtDaily > 0.0f && dtDaily < 0.5f) m_animPhase += dtDaily * 1.5f;
    m_lastAnimTime = currentTime;

    for (size_t i = 0; i < count; ++i) {
        const auto& df = m_daily[i];
        float cellX = left + padding + 10.0f + i * cellW;

        m_res->grayBrush->SetOpacity(contentAlpha * 0.8f);
        auto dateLayout = GetOrCreateTextLayout(df.date, m_res->subFormat, cellW, L"df_date_" + df.date + std::to_wstring(i));
        dateLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(cellX, dataTop), dateLayout.Get(), m_res->grayBrush);

        float iconSize = 22.0f;
        float iconX = cellX + (cellW - iconSize) / 2.0f;
        float iconY = dataTop + 16.0f;
        WeatherType savedType = m_weatherType;
        m_weatherType = MapWeatherDescToType(df.textDay);
        DrawWeatherIcon(iconX, iconY, iconSize, contentAlpha, currentTime);
        m_weatherType = savedType;

        std::wstring maxText = std::to_wstring((int)df.tempMax) + L"\u00B0";
        m_res->whiteBrush->SetOpacity(contentAlpha);
        auto maxLayout = GetOrCreateTextLayout(maxText, m_res->titleFormat, cellW, L"df_max_" + maxText + std::to_wstring(i));
        maxLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(cellX, dataTop+42.0f), maxLayout.Get(), m_res->whiteBrush);

        std::wstring minText = std::to_wstring((int)df.tempMin) + L"\u00B0";
        m_res->grayBrush->SetOpacity(contentAlpha * 0.7f);
        auto minLayout = GetOrCreateTextLayout(minText, m_res->subFormat, cellW, L"df_min_" + minText + std::to_wstring(i));
        minLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(cellX, dataTop+60.0f), minLayout.Get(), m_res->grayBrush);
    }

    std::wstring hint = L"↑ 逐小时";
    m_res->grayBrush->SetOpacity(contentAlpha * 0.45f);
    ComPtr<IDWriteTextFormat> hintFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"zh-cn", &hintFmt);
    ctx->DrawTextW(hint.c_str(), (UINT32)hint.size(), hintFmt.Get(),
        D2D1::RectF(right-padding-60.0f, cardBottom-14.0f, right-padding, cardBottom), m_res->grayBrush);
}

// ── DrawWeatherExpanded ────────────────────────────────────────────────────

void WeatherComponent::DrawWeatherExpanded(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime) {
    if (!m_res) return;
    auto* ctx = m_res->d2dContext;
    auto* fac = m_res->d2dFactory;
    float left=rect.left, top=rect.top, right=rect.right, bottom=rect.bottom;
    float islandWidth = right - left;
    float padding = 15.0f;
    float cardTop=top+padding, cardBottom=bottom-padding;
    float cardWidth = (islandWidth - padding*3) / 2.0f;
    float leftCardLeft  = left + padding;
    float rightCardLeft = leftCardLeft + cardWidth + padding;

    // 右侧玻璃底板
    ComPtr<ID2D1SolidColorBrush> glassBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.15f,0.15f,0.15f,0.4f*contentAlpha), &glassBrush);
    D2D1_ROUNDED_RECT rightCard = D2D1::RoundedRect(D2D1::RectF(rightCardLeft, cardTop, rightCardLeft+cardWidth, cardBottom), 16.0f, 16.0f);
    ctx->FillRoundedRectangle(&rightCard, glassBrush.Get());

    // 左侧意境背景
    WeatherType savedType = m_weatherType;
    m_weatherType = MapWeatherDescToType(m_desc);
    DrawWeatherAmbientBg(leftCardLeft, cardTop, leftCardLeft+cardWidth, cardBottom, contentAlpha, currentTime);
    m_weatherType = savedType;

    // 左侧底部渐变遮罩
    {
        float overlayTop = cardTop + (cardBottom-cardTop)*0.45f;
        D2D1_GRADIENT_STOP ovStops[2] = {
            {0.0f, D2D1::ColorF(0.0f,0.0f,0.0f,0.0f)},
            {1.0f, D2D1::ColorF(0.0f,0.0f,0.0f,0.58f*contentAlpha)}
        };
        ComPtr<ID2D1GradientStopCollection> ovColl;
        ctx->CreateGradientStopCollection(ovStops, 2, &ovColl);
        ComPtr<ID2D1LinearGradientBrush> ovBrush;
        ctx->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(leftCardLeft, overlayTop), D2D1::Point2F(leftCardLeft, cardBottom)),
            ovColl.Get(), &ovBrush);
        if (ovBrush) {
            ComPtr<ID2D1RoundedRectangleGeometry> ovClip;
            fac->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(leftCardLeft, cardTop, leftCardLeft+cardWidth, cardBottom), 16.0f, 16.0f), &ovClip);
            ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), ovClip.Get()), nullptr);
            ctx->FillRectangle(D2D1::RectF(leftCardLeft, overlayTop, leftCardLeft+cardWidth, cardBottom), ovBrush.Get());
            ctx->PopLayer();
        }
    }

    // 左侧温度大字
    std::wstring tempText = std::to_wstring((int)m_temp) + L"\u00B0";
    ComPtr<IDWriteTextFormat> hugeTempFormat;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 34.0f, L"zh-cn", &hugeTempFormat);
    hugeTempFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(tempText.c_str(), (UINT32)tempText.length(), hugeTempFormat.Get(),
        D2D1::RectF(leftCardLeft, cardBottom-62.0f, leftCardLeft+cardWidth, cardBottom-22.0f), m_res->whiteBrush);

    // 左侧天气描述
    ComPtr<IDWriteTextFormat> descFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"zh-cn", &descFmt);
    descFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_res->grayBrush->SetOpacity(contentAlpha * 0.60f);
    ctx->DrawTextW(m_desc.c_str(), (UINT32)m_desc.length(), descFmt.Get(),
        D2D1::RectF(leftCardLeft, cardBottom-20.0f, leftCardLeft+cardWidth, cardBottom-4.0f), m_res->grayBrush);

    // 右侧 2×3 逐小时网格
    if (!m_hourly.empty()) {
        int rows=2, cols=3;
        float cellWidth  = cardWidth / cols;
        float cellHeight = (cardBottom - cardTop) / rows;
        for (size_t i = 0; i < m_hourly.size() && i < 6; ++i) {
            int row = (int)(i / cols), col = (int)(i % cols);
            float cellX = rightCardLeft + col * cellWidth;
            float cellY = cardTop + row * cellHeight;

            m_res->grayBrush->SetOpacity(contentAlpha * 0.75f);
            auto timeLayout = GetOrCreateTextLayout(m_hourly[i].time, m_res->subFormat, cellWidth, L"hf_time_" + m_hourly[i].time);
            timeLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ctx->DrawTextLayout(D2D1::Point2F(cellX, cellY+6.0f), timeLayout.Get(), m_res->grayBrush);

            float iconSize=18.0f;
            float iconX = cellX + (cellWidth-iconSize)*0.5f;
            float iconY = cellY + 22.0f;
            WeatherType st2 = m_weatherType;
            m_weatherType = MapWeatherDescToType(m_hourly[i].text);
            DrawWeatherIcon(iconX, iconY, iconSize, contentAlpha*0.90f, currentTime);
            m_weatherType = st2;

            std::wstring hTempText = std::to_wstring((int)m_hourly[i].temp) + L"\u00B0";
            m_res->whiteBrush->SetOpacity(contentAlpha);
            auto tempLayout = GetOrCreateTextLayout(hTempText, m_res->titleFormat, cellWidth, L"hf_temp_" + hTempText);
            tempLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ctx->DrawTextLayout(D2D1::Point2F(cellX, cellY+44.0f), tempLayout.Get(), m_res->whiteBrush);
        }
    } else {
        std::wstring emptyText = L"暂无预报数据";
        m_res->grayBrush->SetOpacity(contentAlpha);
        auto emptyLayout = GetOrCreateTextLayout(emptyText, m_res->subFormat, cardWidth, L"hf_empty");
        emptyLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(rightCardLeft, cardTop+(cardBottom-cardTop)/2.0f-10.0f), emptyLayout.Get(), m_res->grayBrush);
    }

    // 底部提示
    std::wstring hint = L"↓ 逐日预报";
    m_res->grayBrush->SetOpacity(contentAlpha * 0.45f);
    ComPtr<IDWriteTextFormat> hintFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"zh-cn", &hintFmt);
    ctx->DrawTextW(hint.c_str(), (UINT32)hint.size(), hintFmt.Get(),
        D2D1::RectF(right-padding-70.0f, bottom-padding-14.0f, right-padding, bottom-padding), m_res->grayBrush);
}
