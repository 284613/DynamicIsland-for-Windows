#include "components/WeatherComponent.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

// ── 生命周期 ───────────────────────────────────────────────────────────────

void WeatherComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void WeatherComponent::Update(float deltaTime) {
    if (m_isViewTransitioning && m_viewTransitionDuration > 0.0f) {
        m_viewTransitionProgress += deltaTime / m_viewTransitionDuration;
        if (m_viewTransitionProgress >= 1.0f) {
            m_viewTransitionProgress = 1.0f;
            m_viewMode = m_targetViewMode;
            m_transitionFromMode = m_targetViewMode;
            m_isViewTransitioning = false;
        }
    }
}

// ── 数据更新 ───────────────────────────────────────────────────────────────

void WeatherComponent::SetWeatherData(
    const std::wstring& locationText,
    const std::wstring& desc, float temp, const std::wstring& iconId,
    const std::vector<HourlyForecast>& hourly,
    const std::vector<DailyForecast>& daily,
    bool available)
{
    WeatherType newType = MapWeatherDescToType(desc);
    if (newType != m_weatherType) {
        m_weatherType = newType;
        ResetAnimation();
    }
    m_locationText = locationText;
    m_desc   = desc;
    m_temp   = temp;
    m_iconId = iconId;
    m_hourly = hourly;
    m_daily  = daily;
    m_available = available;
}

void WeatherComponent::SetViewMode(WeatherViewMode mode) {
    BeginViewTransition(mode);
}

// ── 主绘制入口 ─────────────────────────────────────────────────────────────

void WeatherComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res || !m_isExpanded) return;

    if (!m_available) {
        DrawUnavailable(rect, contentAlpha, currentTimeMs);
        return;
    }

    if (!m_isViewTransitioning) {
        DrawWeatherView(m_viewMode, rect, contentAlpha, currentTimeMs);
        return;
    }

    const float eased = EaseOutCubic((std::max)(0.0f, (std::min)(1.0f, m_viewTransitionProgress)));
    const float enterOffsetY = (1.0f - eased) * 12.0f;
    const float exitOffsetY = -eased * 6.0f;

    D2D1_RECT_F fromRect = rect;
    fromRect.top += exitOffsetY;
    fromRect.bottom += exitOffsetY;
    DrawWeatherView(m_transitionFromMode, fromRect, contentAlpha * (1.0f - eased), currentTimeMs);

    D2D1_RECT_F toRect = rect;
    toRect.top += enterOffsetY;
    toRect.bottom += enterOffsetY;
    DrawWeatherView(m_targetViewMode, toRect, contentAlpha * eased, currentTimeMs);
}

void WeatherComponent::DrawCompact(float iconX, float iconY, float iconSize,
                                   float contentAlpha, ULONGLONG currentTimeMs) {
    if (!m_res) return;
    auto* ctx = m_res->d2dContext;

    // 推进动画 phase
    if (m_lastAnimTime == 0) m_lastAnimTime = currentTimeMs;
    float dt = (float)(currentTimeMs - m_lastAnimTime) / 1000.0f;
    if (dt > 0.0f && dt < 0.5f) m_animPhase += dt * 2.0f;
    m_lastAnimTime = currentTimeMs;

    DrawWeatherIcon(iconX, iconY, iconSize, contentAlpha, currentTimeMs);

    // Temperature / unavailable state
    if (m_res->subFormat) {
        const std::wstring tempText = m_available ? (std::to_wstring((int)m_temp) + L"\u00B0") : L"未配置";
        ComPtr<IDWriteTextLayout> tempLayout;
        m_res->dwriteFactory->CreateTextLayout(tempText.c_str(), (UINT32)tempText.size(),
            m_res->subFormat, 80.0f, 100.0f, &tempLayout);
        if (tempLayout) {
            DWRITE_TEXT_METRICS metrics;
            tempLayout->GetMetrics(&metrics);
            float tempX = iconX - metrics.width - 5.0f;
            float tempY = iconY + (iconSize - metrics.height) / 2.0f;
            m_res->whiteBrush->SetOpacity(contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(tempX, tempY), tempLayout.Get(), m_res->whiteBrush);
        }
    }
}

void WeatherComponent::DrawUnavailable(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> panelBrush;
    ComPtr<ID2D1SolidColorBrush> strokeBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.15f, 0.42f * contentAlpha), &panelBrush);
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.62f, 0.66f, 0.65f * contentAlpha), &strokeBrush);

    const D2D1_RECT_F cardRect = D2D1::RectF(rect.left + 16.0f, rect.top + 16.0f, rect.right - 16.0f, rect.bottom - 16.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(cardRect, 18.0f, 18.0f), panelBrush.Get());
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(cardRect, 18.0f, 18.0f), strokeBrush.Get(), 1.0f);

    const WeatherType savedType = m_weatherType;
    m_weatherType = WeatherType::Default;
    DrawWeatherIcon(cardRect.left + 22.0f, cardRect.top + 20.0f, 26.0f, contentAlpha * 0.95f, currentTimeMs);
    m_weatherType = savedType;

    const D2D1_RECT_F titleRect = D2D1::RectF(cardRect.left + 58.0f, cardRect.top + 16.0f, cardRect.right - 20.0f, cardRect.top + 38.0f);
    const D2D1_RECT_F reasonRect = D2D1::RectF(cardRect.left + 22.0f, cardRect.top + 56.0f, cardRect.right - 22.0f, cardRect.top + 90.0f);
    const D2D1_RECT_F hintRect = D2D1::RectF(cardRect.left + 22.0f, cardRect.top + 94.0f, cardRect.right - 22.0f, cardRect.bottom - 20.0f);

    auto titleLayout = GetOrCreateTextLayout(L"天气不可用", m_res->titleFormat, titleRect.right - titleRect.left, L"weather_unavailable_title");
    titleLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextLayout(D2D1::Point2F(titleRect.left, titleRect.top), titleLayout.Get(), m_res->whiteBrush);

    auto reasonLayout = GetOrCreateTextLayout(m_desc.empty() ? L"未配置 API Key" : m_desc, m_res->subFormat, reasonRect.right - reasonRect.left, L"weather_unavailable_reason");
    reasonLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_res->grayBrush->SetOpacity(contentAlpha * 0.88f);
    ctx->DrawTextLayout(D2D1::Point2F(reasonRect.left, reasonRect.top), reasonLayout.Get(), m_res->grayBrush);

    const std::wstring locationLine = m_locationText.empty() ? L"请在设置页配置天气 API Key" : (L"当前位置: " + m_locationText + L"。请在设置页补充 API Key。");
    auto hintLayout = GetOrCreateTextLayout(locationLine, m_res->subFormat, hintRect.right - hintRect.left, L"weather_unavailable_hint");
    hintLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    m_res->grayBrush->SetOpacity(contentAlpha * 0.72f);
    ctx->DrawTextLayout(D2D1::Point2F(hintRect.left, hintRect.top), hintLayout.Get(), m_res->grayBrush);
}

bool WeatherComponent::OnMouseWheel(float /*x*/, float /*y*/, int delta) {
    if (!m_isExpanded) return false;
    if (delta < 0)
        BeginViewTransition(WeatherViewMode::Daily);
    else
        BeginViewTransition(WeatherViewMode::Hourly);
    return true;
}

void WeatherComponent::DrawWeatherView(WeatherViewMode mode, const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime) {
    if (contentAlpha <= 0.01f) {
        return;
    }

    if (mode == WeatherViewMode::Daily) {
        DrawWeatherDaily(rect, contentAlpha, currentTime);
    } else {
        DrawWeatherExpanded(rect, contentAlpha, currentTime);
    }
}

void WeatherComponent::BeginViewTransition(WeatherViewMode targetMode) {
    if (m_isViewTransitioning) {
        if (targetMode == m_targetViewMode) {
            return;
        }

        const float clampedProgress = (std::max)(0.0f, (std::min)(1.0f, m_viewTransitionProgress));
        m_transitionFromMode = m_targetViewMode;
        m_targetViewMode = targetMode;
        m_viewMode = m_transitionFromMode;
        m_viewTransitionProgress = 1.0f - clampedProgress;
        return;
    }

    if (targetMode == m_viewMode) {
        m_targetViewMode = targetMode;
        m_transitionFromMode = targetMode;
        m_viewTransitionProgress = 1.0f;
        return;
    }

    m_transitionFromMode = m_viewMode;
    m_targetViewMode = targetMode;
    m_viewTransitionProgress = 0.0f;
    m_isViewTransitioning = true;
}

float WeatherComponent::EaseOutCubic(float t) const {
    float oneMinus = 1.0f - t;
    return 1.0f - oneMinus * oneMinus * oneMinus;
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
    if (d.find(L"雪") != std::wstring::npos || d.find(L"霜") != std::wstring::npos) return WeatherType::Snow;
    if (d.find(L"雾") != std::wstring::npos || d.find(L"霾") != std::wstring::npos) return WeatherType::Fog;
    if (d.find(L"雷暴") != std::wstring::npos || d.find(L"雷电") != std::wstring::npos) return WeatherType::Thunder;
    if (d.find(L"雨") != std::wstring::npos || d.find(L"阵雨") != std::wstring::npos) return WeatherType::Rainy;
    if (d.find(L"晴") != std::wstring::npos && d.find(L"多云") == std::wstring::npos && d.find(L"阴") == std::wstring::npos) return WeatherType::Clear;
    if (d.find(L"多云") != std::wstring::npos || d.find(L"间晴") != std::wstring::npos || d.find(L"间多云") != std::wstring::npos) return WeatherType::PartlyCloudy;
    if (d.find(L"阴") != std::wstring::npos) return WeatherType::Cloudy;
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
    SYSTEMTIME st;
    GetLocalTime(&st);
    enum class SceneTime { Dawn, Day, Dusk, Night };
    SceneTime sceneTime = SceneTime::Day;
    if (st.wHour < 5 || st.wHour >= 19) {
        sceneTime = SceneTime::Night;
    } else if (st.wHour < 7) {
        sceneTime = SceneTime::Dawn;
    } else if (st.wHour < 17) {
        sceneTime = SceneTime::Day;
    } else {
        sceneTime = SceneTime::Dusk;
    }
    auto GetPrecipitationStrength = [&](bool snow) -> float {
        const std::wstring& d = m_desc;
        if (snow) {
            if (d.find(L"暴雪") != std::wstring::npos) return 1.30f;
            if (d.find(L"大雪") != std::wstring::npos) return 1.05f;
            if (d.find(L"中雪") != std::wstring::npos) return 0.82f;
            if (d.find(L"小雪") != std::wstring::npos) return 0.58f;
            if (d.find(L"阵雪") != std::wstring::npos) return 0.70f;
            if (d.find(L"雨夹雪") != std::wstring::npos) return 0.74f;
            return 0.72f;
        }

        if (d.find(L"特大暴雨") != std::wstring::npos) return 1.40f;
        if (d.find(L"大暴雨") != std::wstring::npos || d.find(L"暴雨") != std::wstring::npos) return 1.22f;
        if (d.find(L"大雨") != std::wstring::npos) return 1.00f;
        if (d.find(L"中雨") != std::wstring::npos) return 0.78f;
        if (d.find(L"小雨") != std::wstring::npos) return 0.56f;
        if (d.find(L"毛毛雨") != std::wstring::npos || d.find(L"细雨") != std::wstring::npos) return 0.42f;
        if (d.find(L"冻雨") != std::wstring::npos) return 0.86f;
        if (d.find(L"雷阵雨") != std::wstring::npos || d.find(L"雷雨") != std::wstring::npos) return 0.92f;
        if (d.find(L"阵雨") != std::wstring::npos) return 0.64f;
        return 0.74f;
    };

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
    auto FillSoftLight = [&](float ox, float oy, float rx, float ry, D2D1_COLOR_F inner, D2D1_COLOR_F outer) {
        auto glow = MakeRadGrad(ox, oy, rx, ry, inner, outer);
        if (glow) {
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ox, oy), rx, ry), glow.Get());
        }
    };
    auto FillMistBand = [&](float cy, float widthScale, float heightScale, float drift, D2D1_COLOR_F color) {
        ComPtr<ID2D1SolidColorBrush> mistBrush;
        ctx->CreateSolidColorBrush(color, &mistBrush);
        if (!mistBrush) return;
        mistBrush->SetOpacity(color.a * alpha);
        ctx->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx + drift, cy), W * widthScale, H * heightScale),
            mistBrush.Get());
    };
    auto FillAtmosphereLayer = [&](float topY, float bottomY, D2D1_COLOR_F topColor, D2D1_COLOR_F bottomColor) {
        D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, topColor},
            {1.0f, bottomColor}
        };
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 2, &coll))) return;
        ComPtr<ID2D1LinearGradientBrush> hazeBrush;
        ctx->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(cx, topY), D2D1::Point2F(cx, bottomY)),
            coll.Get(),
            &hazeBrush);
        if (hazeBrush) {
            ctx->FillRectangle(D2D1::RectF(L, topY, R, bottomY), hazeBrush.Get());
        }
    };
    auto FillCloudBand = [&](float centerX, float centerY, float width, float height, float opacity, D2D1_COLOR_F color) {
        ComPtr<ID2D1SolidColorBrush> cloudBrush;
        ctx->CreateSolidColorBrush(color, &cloudBrush);
        if (!cloudBrush) return;
        cloudBrush->SetOpacity(opacity * alpha);

        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY), width * 0.42f, height * 0.34f), cloudBrush.Get());
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX - width * 0.28f, centerY - height * 0.08f), width * 0.24f, height * 0.24f), cloudBrush.Get());
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX + width * 0.30f, centerY - height * 0.05f), width * 0.26f, height * 0.22f), cloudBrush.Get());
        ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX - width * 0.05f, centerY - height * 0.18f), width * 0.28f, height * 0.25f), cloudBrush.Get());
    };
    auto FillRidge = [&](float baseY, float amp1, float amp2, float freq1, float freq2, float drift,
                         D2D1_COLOR_F color, float opacity) {
        ComPtr<ID2D1SolidColorBrush> ridgeBrush;
        ctx->CreateSolidColorBrush(color, &ridgeBrush);
        if (!ridgeBrush) return;
        ridgeBrush->SetOpacity(opacity * alpha);

        ComPtr<ID2D1PathGeometry> ridge;
        fac->CreatePathGeometry(&ridge);
        ComPtr<ID2D1GeometrySink> sink;
        ridge->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(L, B), D2D1_FIGURE_BEGIN_FILLED);
        constexpr int kSegments = 60;
        for (int i = 0; i <= kSegments; ++i) {
            float xf = (float)i / (float)kSegments;
            float x = L + xf * W;
            float y = baseY
                + sinf(xf * 6.28318f * freq1 + drift) * amp1
                + cosf(xf * 6.28318f * freq2 + drift * 0.7f) * amp2
                + sinf(xf * 6.28318f * (freq1 * 2.6f) + drift * 1.4f) * (amp1 * 0.22f)
                + cosf(xf * 6.28318f * (freq2 * 1.7f) + drift * 1.1f) * (amp2 * 0.18f);
            sink->AddLine(D2D1::Point2F(x, y));
        }
        sink->AddLine(D2D1::Point2F(R, B));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        ctx->FillGeometry(ridge.Get(), ridgeBrush.Get());
    };
    auto FillTreeLine = [&](float baseY, float spacing, float minHeight, float maxHeight,
                            D2D1_COLOR_F color, float opacity, float drift) {
        ComPtr<ID2D1SolidColorBrush> treeBrush;
        ctx->CreateSolidColorBrush(color, &treeBrush);
        if (!treeBrush) return;
        treeBrush->SetOpacity(opacity * alpha);

        ComPtr<ID2D1PathGeometry> forest;
        fac->CreatePathGeometry(&forest);
        ComPtr<ID2D1GeometrySink> sink;
        forest->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(L, B), D2D1_FIGURE_BEGIN_FILLED);

        constexpr int kSegments = 72;
        for (int i = 0; i <= kSegments; ++i) {
            float xf = (float)i / (float)kSegments;
            float x = L + xf * W;
            float jagged = sinf(xf * 6.28318f * 8.0f + drift * 1.6f)
                         + 0.55f * cosf(xf * 6.28318f * 15.0f + drift * 0.9f)
                         + 0.25f * sinf(xf * 6.28318f * 27.0f + drift * 2.1f);
            float treeShape = 0.5f + 0.5f * jagged;
            float height = minHeight + (maxHeight - minHeight) * treeShape;
            float y = baseY - height;
            sink->AddLine(D2D1::Point2F(x, y));
        }

        sink->AddLine(D2D1::Point2F(R, B));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        ctx->FillGeometry(forest.Get(), treeBrush.Get());
    };
    auto DrawRainCurtain = [&](int count, float speed, float startY, float travel, float slant,
                               float dropLength, float thickness, float opacity, float swayScale) {
        ComPtr<ID2D1SolidColorBrush> rainBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.76f, 0.86f, 0.98f, opacity * alpha), &rainBrush);
        if (!rainBrush) return;

        for (int i = 0; i < count; ++i) {
            float seed = i * 0.618f;
            float phase = fmodf(t * speed + seed, 1.0f);
            float startX = L + fmodf(i * W * 0.173f + sinf(t * 0.45f + i) * W * swayScale + W, W);
            float y = startY + phase * travel;
            float x = startX + phase * dropLength * slant;
            x = L + fmodf(x - L + W, W);
            rainBrush->SetOpacity(opacity * (0.55f + 0.45f * (1.0f - phase)) * alpha);
            DrawLineLocal(x, y, x + dropLength * slant, y + dropLength, rainBrush.Get(), thickness);
        }
    };
    auto DrawSnowField = [&](int count, float speed, float startY, float travel,
                             float driftAmp, float minSize, float maxSize, float opacity) {
        ComPtr<ID2D1SolidColorBrush> snowBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, opacity * alpha), &snowBrush);
        if (!snowBrush) return;

        for (int i = 0; i < count; ++i) {
            float seed = i * 0.173f;
            float phase = fmodf(t * speed + seed, 1.0f);
            float swirl = sinf(t * (0.45f + 0.04f * i) + i * 1.37f);
            float x = L + fmodf(i * W * 0.121f + swirl * W * driftAmp + W, W);
            float y = startY + phase * travel;
            float size = minSize + (maxSize - minSize) * (0.5f + 0.5f * sinf(i * 1.73f));
            snowBrush->SetOpacity(opacity * (0.55f + 0.45f * sinf(phase * 3.14159f)) * alpha);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), size, size * 0.92f), snowBrush.Get());
        }
    };
    auto DrawVignette = [&]() {
        D2D1_GRADIENT_STOP vignetteStops[2] = {
            {0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)},
            {1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f * alpha)}
        };
        ComPtr<ID2D1GradientStopCollection> vignetteColl;
        ctx->CreateGradientStopCollection(vignetteStops, 2, &vignetteColl);
        if (!vignetteColl) return;
        ComPtr<ID2D1RadialGradientBrush> vignetteBrush;
        ctx->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(cx, T + H * 0.48f),
                D2D1::Point2F(0, 0),
                W * 0.70f,
                H * 0.62f),
            vignetteColl.Get(),
            &vignetteBrush);
        if (vignetteBrush) {
            ctx->FillRectangle(D2D1::RectF(L, T, R, B), vignetteBrush.Get());
        }
    };
    auto FillGroundGlow = [&](float centerY, float heightScale, D2D1_COLOR_F color, float opacity) {
        auto glow = MakeRadGrad(cx, centerY, W * 0.75f, H * heightScale,
            D2D1::ColorF(color.r, color.g, color.b, opacity * alpha),
            D2D1::ColorF(color.r, color.g, color.b, 0.0f));
        if (glow) {
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, centerY), W * 0.75f, H * heightScale), glow.Get());
        }
    };

    // 裁剪
    ComPtr<ID2D1RoundedRectangleGeometry> clipGeo;
    fac->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(L,T,R,B), 16.0f, 16.0f), &clipGeo);
    ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo.Get()), nullptr);

    switch (m_weatherType) {
    case WeatherType::Rainy:
    case WeatherType::Thunder: {
        bool isThunder = (m_weatherType == WeatherType::Thunder);
        float rainStrength = GetPrecipitationStrength(false);
        D2D1_COLOR_F rainTop = isThunder ? D2D1::ColorF(0.05f,0.07f,0.12f) : D2D1::ColorF(0.18f,0.21f,0.27f);
        D2D1_COLOR_F rainBot = isThunder ? D2D1::ColorF(0.12f,0.15f,0.20f) : D2D1::ColorF(0.24f,0.27f,0.31f);
        if (sceneTime == SceneTime::Dawn) {
            rainTop = D2D1::ColorF(0.22f, 0.20f, 0.24f);
            rainBot = D2D1::ColorF(0.34f, 0.30f, 0.32f);
        } else if (sceneTime == SceneTime::Dusk) {
            rainTop = D2D1::ColorF(0.18f, 0.15f, 0.18f);
            rainBot = D2D1::ColorF(0.26f, 0.22f, 0.24f);
        } else if (sceneTime == SceneTime::Night) {
            rainTop = D2D1::ColorF(0.04f, 0.05f, 0.09f);
            rainBot = D2D1::ColorF(0.10f, 0.12f, 0.17f);
        }
        auto bg = MakeLinGrad(rainTop, rainBot);
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        FillSoftLight(L + W * 0.32f, T + H * 0.10f, W * 0.38f, H * 0.22f,
            D2D1::ColorF(0.64f, 0.72f, 0.84f, 0.07f * alpha),
            D2D1::ColorF(0.64f, 0.72f, 0.84f, 0.0f));
        FillAtmosphereLayer(T + H * 0.42f, B - H * 0.12f,
            D2D1::ColorF(0.62f, 0.68f, 0.74f, 0.08f * alpha),
            D2D1::ColorF(0.62f, 0.68f, 0.74f, 0.0f));
        FillRidge(B - H * 0.34f, H * 0.040f, H * 0.020f, 1.0f, 2.2f, t * 0.08f,
            D2D1::ColorF(0.20f, 0.24f, 0.28f), 0.45f);
        FillRidge(B - H * 0.24f, H * 0.050f, H * 0.024f, 1.4f, 3.0f, t * 0.12f,
            D2D1::ColorF(0.12f, 0.15f, 0.17f), 0.72f);
        FillTreeLine(B - H * 0.18f, W * 0.060f, H * 0.05f, H * 0.11f,
            D2D1::ColorF(0.06f, 0.08f, 0.09f), 0.85f, t * 0.10f);
        FillCloudBand(cx,               T + H * 0.18f, W * 0.90f, H * 0.18f, 0.85f, D2D1::ColorF(0.22f, 0.24f, 0.28f));
        FillCloudBand(L + W * 0.28f,    T + H * 0.24f, W * 0.60f, H * 0.14f, 0.70f, D2D1::ColorF(0.30f, 0.32f, 0.36f));
        FillCloudBand(R - W * 0.20f,    T + H * 0.22f, W * 0.46f, H * 0.13f, 0.58f, D2D1::ColorF(0.34f, 0.36f, 0.40f));
        DrawRainCurtain((int)((isThunder ? 14 : 12) * rainStrength), (isThunder ? 1.6f : 1.2f) * (0.85f + rainStrength * 0.22f),
            T + H * 0.30f, H * (0.48f + rainStrength * 0.12f), 0.08f, H * (0.08f + rainStrength * 0.03f), 0.65f, 0.12f + rainStrength * 0.08f, 0.010f);
        DrawRainCurtain((int)((isThunder ? 20 : 16) * rainStrength), (isThunder ? 2.6f : 2.0f) * (0.90f + rainStrength * 0.24f),
            T + H * 0.26f, H * (0.56f + rainStrength * 0.12f), 0.12f, H * (0.10f + rainStrength * 0.05f), 0.95f, 0.22f + rainStrength * 0.12f, 0.015f);
        DrawRainCurtain((int)((isThunder ? 30 : 24) * rainStrength), (isThunder ? 3.8f : 2.9f) * (0.95f + rainStrength * 0.28f),
            T + H * 0.22f, H * (0.62f + rainStrength * 0.12f), 0.16f, H * (0.13f + rainStrength * 0.06f), 1.30f, 0.34f + rainStrength * 0.16f, 0.020f);
        FillGroundGlow(B - H * 0.06f, 0.09f, D2D1::ColorF(0.62f, 0.70f, 0.80f), 0.08f + rainStrength * 0.06f);
        FillMistBand(B - H * 0.10f, 0.72f, 0.09f + rainStrength * 0.03f, sinf(t * 0.28f) * W * 0.03f, D2D1::ColorF(0.74f, 0.80f, 0.86f, 0.10f + rainStrength * 0.04f));
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
        float snowStrength = GetPrecipitationStrength(true);
        D2D1_COLOR_F snowTop = D2D1::ColorF(0.16f,0.22f,0.32f);
        D2D1_COLOR_F snowBot = D2D1::ColorF(0.30f,0.36f,0.44f);
        if (sceneTime == SceneTime::Dawn) {
            snowTop = D2D1::ColorF(0.34f, 0.36f, 0.42f);
            snowBot = D2D1::ColorF(0.58f, 0.56f, 0.54f);
        } else if (sceneTime == SceneTime::Day) {
            snowTop = D2D1::ColorF(0.40f, 0.48f, 0.60f);
            snowBot = D2D1::ColorF(0.72f, 0.78f, 0.82f);
        } else if (sceneTime == SceneTime::Dusk) {
            snowTop = D2D1::ColorF(0.28f, 0.26f, 0.34f);
            snowBot = D2D1::ColorF(0.44f, 0.40f, 0.42f);
        } else {
            snowTop = D2D1::ColorF(0.12f, 0.16f, 0.24f);
            snowBot = D2D1::ColorF(0.22f, 0.28f, 0.34f);
        }
        auto bg = MakeLinGrad(snowTop, snowBot);
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        FillSoftLight(cx, T + H * 0.18f, W * 0.42f, H * 0.26f,
            D2D1::ColorF(0.86f, 0.92f, 1.0f, 0.08f * alpha),
            D2D1::ColorF(0.86f, 0.92f, 1.0f, 0.0f));
        FillAtmosphereLayer(T + H * 0.40f, B - H * 0.10f,
            D2D1::ColorF(0.92f, 0.95f, 1.0f, 0.07f * alpha),
            D2D1::ColorF(0.92f, 0.95f, 1.0f, 0.0f));
        FillRidge(B - H * 0.36f, H * 0.032f, H * 0.014f, 1.0f, 2.0f, t * 0.05f,
            D2D1::ColorF(0.54f, 0.60f, 0.66f), 0.52f);
        FillRidge(B - H * 0.24f, H * 0.040f, H * 0.018f, 1.5f, 2.9f, t * 0.08f,
            D2D1::ColorF(0.34f, 0.40f, 0.46f), 0.72f);
        FillTreeLine(B - H * 0.18f, W * 0.065f, H * 0.04f, H * 0.09f,
            D2D1::ColorF(0.18f, 0.22f, 0.24f), 0.72f, t * 0.08f);
        FillCloudBand(cx,               T + H * 0.20f, W * 0.80f, H * 0.16f, 0.68f, D2D1::ColorF(0.68f, 0.72f, 0.78f));
        FillCloudBand(L + W * 0.22f,    T + H * 0.16f, W * 0.44f, H * 0.12f, 0.54f, D2D1::ColorF(0.78f, 0.82f, 0.86f));
        DrawSnowField((int)(16 * snowStrength), 0.18f * (0.88f + snowStrength * 0.16f),
            T + H * 0.16f, H * (0.74f + snowStrength * 0.10f), 0.020f + snowStrength * 0.015f,
            W * 0.005f, W * (0.008f + snowStrength * 0.004f), 0.18f + snowStrength * 0.10f);
        DrawSnowField((int)(18 * snowStrength), 0.28f * (0.90f + snowStrength * 0.18f),
            T + H * 0.12f, H * (0.78f + snowStrength * 0.10f), 0.035f + snowStrength * 0.020f,
            W * 0.008f, W * (0.013f + snowStrength * 0.005f), 0.30f + snowStrength * 0.14f);
        DrawSnowField((int)(22 * snowStrength), 0.42f * (0.92f + snowStrength * 0.20f),
            T + H * 0.08f, H * (0.82f + snowStrength * 0.12f), 0.050f + snowStrength * 0.028f,
            W * 0.012f, W * (0.018f + snowStrength * 0.007f), 0.48f + snowStrength * 0.18f);
        FillGroundGlow(B - H * 0.07f, 0.11f, D2D1::ColorF(0.96f, 0.98f, 1.0f), 0.10f + snowStrength * 0.06f);
        FillMistBand(B - H * 0.10f, 0.74f, 0.08f + snowStrength * 0.03f, sinf(t * 0.16f) * W * 0.025f, D2D1::ColorF(0.95f, 0.97f, 1.0f, 0.10f + snowStrength * 0.06f));
        break;
    }
    case WeatherType::Clear: {
        if (sceneTime == SceneTime::Night) {
            auto bg = MakeLinGrad(D2D1::ColorF(0.02f,0.03f,0.08f), D2D1::ColorF(0.10f,0.12f,0.18f));
            if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
            FillSoftLight(L + W * 0.30f, T + H * 0.08f, W * 0.30f, H * 0.16f,
                D2D1::ColorF(0.36f, 0.46f, 0.74f, 0.08f * alpha),
                D2D1::ColorF(0.36f, 0.46f, 0.74f, 0.0f));
            FillAtmosphereLayer(T + H * 0.48f, B - H * 0.12f,
                D2D1::ColorF(0.50f, 0.58f, 0.72f, 0.04f * alpha),
                D2D1::ColorF(0.50f, 0.58f, 0.72f, 0.0f));
            FillRidge(B - H * 0.34f, H * 0.030f, H * 0.014f, 1.1f, 2.0f, t * 0.03f,
                D2D1::ColorF(0.08f, 0.10f, 0.14f), 0.46f);
            FillRidge(B - H * 0.22f, H * 0.040f, H * 0.018f, 1.6f, 3.1f, t * 0.05f,
                D2D1::ColorF(0.04f, 0.05f, 0.08f), 0.82f);
            FillTreeLine(B - H * 0.17f, W * 0.070f, H * 0.05f, H * 0.11f,
                D2D1::ColorF(0.02f, 0.03f, 0.04f), 0.90f, t * 0.04f);
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
            FillMistBand(B - H * 0.12f, 0.68f, 0.08f, sinf(t * 0.12f) * W * 0.02f, D2D1::ColorF(0.50f, 0.56f, 0.70f, 0.06f));
        } else {
            D2D1_COLOR_F clearTop = D2D1::ColorF(0.28f,0.58f,0.90f);
            D2D1_COLOR_F clearBot = D2D1::ColorF(0.88f,0.84f,0.68f);
            if (sceneTime == SceneTime::Dawn) {
                clearTop = D2D1::ColorF(0.46f, 0.58f, 0.78f);
                clearBot = D2D1::ColorF(0.98f, 0.76f, 0.58f);
            } else if (sceneTime == SceneTime::Day) {
                clearTop = D2D1::ColorF(0.30f, 0.62f, 0.96f);
                clearBot = D2D1::ColorF(0.90f, 0.92f, 0.78f);
            } else if (sceneTime == SceneTime::Dusk) {
                clearTop = D2D1::ColorF(0.28f, 0.38f, 0.66f);
                clearBot = D2D1::ColorF(0.98f, 0.62f, 0.40f);
            }
            auto bg = MakeLinGrad(clearTop, clearBot);
            if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
            FillSoftLight(cx, B - H * 0.08f, W * 0.78f, H * 0.24f,
                D2D1::ColorF(1.0f, 0.86f, 0.62f, 0.15f * alpha),
                D2D1::ColorF(1.0f, 0.86f, 0.62f, 0.0f));
            FillAtmosphereLayer(T + H * 0.44f, B - H * 0.10f,
                D2D1::ColorF(1.0f, 0.92f, 0.78f, 0.05f * alpha),
                D2D1::ColorF(1.0f, 0.92f, 0.78f, 0.0f));
            FillRidge(B - H * 0.34f, H * 0.034f, H * 0.016f, 1.1f, 2.1f, t * 0.04f,
                D2D1::ColorF(0.38f, 0.46f, 0.54f), 0.36f);
            FillRidge(B - H * 0.23f, H * 0.046f, H * 0.020f, 1.6f, 3.0f, t * 0.06f,
                D2D1::ColorF(0.18f, 0.26f, 0.20f), 0.76f);
            FillTreeLine(B - H * 0.17f, W * 0.068f, H * 0.05f, H * 0.12f,
                D2D1::ColorF(0.10f, 0.16f, 0.10f), 0.92f, t * 0.05f);
            float sunX=R-W*0.22f, sunY=T+H*0.24f, sunR=W*0.14f;
            D2D1_COLOR_F glowInner = (sceneTime == SceneTime::Dusk)
                ? D2D1::ColorF(1.0f, 0.74f, 0.32f, 0.30f * alpha)
                : D2D1::ColorF(1.0f, 0.92f, 0.40f, 0.30f * alpha);
            auto glow = MakeRadGrad(sunX,sunY,sunR*2.4f,sunR*2.4f, glowInner, D2D1::ColorF(1.0f,0.92f,0.40f,0.0f));
            if (glow) ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sunX,sunY), sunR*2.4f, sunR*2.4f), glow.Get());
            ComPtr<ID2D1SolidColorBrush> sunBrush;
            D2D1_COLOR_F sunColor = (sceneTime == SceneTime::Dusk)
                ? D2D1::ColorF(1.0f, 0.76f, 0.32f, alpha)
                : D2D1::ColorF(1.0f, 0.93f, 0.44f, alpha);
            ctx->CreateSolidColorBrush(sunColor, &sunBrush);
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sunX,sunY), sunR, sunR), sunBrush.Get());
            sunBrush->SetOpacity(0.70f*alpha);
            float rayOff=sunR*1.28f, rayLen=sunR*0.46f;
            for (int i = 0; i < 8; i++) {
                float a = t*0.38f + i*3.14159f*0.25f;
                DrawLineLocal(sunX+cosf(a)*rayOff, sunY+sinf(a)*rayOff, sunX+cosf(a)*(rayOff+rayLen), sunY+sinf(a)*(rayOff+rayLen), sunBrush.Get(), 2.0f);
            }
            FillCloudBand(L + W * 0.42f + sinf(t * 0.18f) * W * 0.02f, T + H * 0.42f, W * 0.34f, H * 0.10f, 0.34f,
                D2D1::ColorF(1.0f, 1.0f, 1.0f));
            FillGroundGlow(B - H * 0.08f, 0.10f, D2D1::ColorF(0.98f, 0.86f, 0.66f), sceneTime == SceneTime::Dusk ? 0.10f : 0.06f);
        }
        break;
    }
    case WeatherType::PartlyCloudy:
    case WeatherType::Cloudy:
    case WeatherType::Default: {
        bool partlyCloudy = (m_weatherType == WeatherType::PartlyCloudy);
        float maxCov=partlyCloudy?0.55f:0.90f, entryDur=3.0f;
        float rawT = fminf(t/entryDur, 1.0f);
        float cover = maxCov * rawT*rawT*(3.0f-2.0f*rawT);
        auto Lerp = [](float a, float b, float f) { return a+(b-a)*f; };
        float skyR,skyG,skyB,ovR,ovG,ovB;
        bool isNight = (sceneTime == SceneTime::Night);
        if (sceneTime == SceneTime::Dawn) {
            skyR=0.48f; skyG=0.56f; skyB=0.68f; ovR=0.58f; ovG=0.52f; ovB=0.52f;
        } else if (sceneTime == SceneTime::Day) {
            skyR=0.30f; skyG=0.56f; skyB=0.84f; ovR=0.42f; ovG=0.44f; ovB=0.48f;
        } else if (sceneTime == SceneTime::Dusk) {
            skyR=0.28f; skyG=0.34f; skyB=0.54f; ovR=0.42f; ovG=0.36f; ovB=0.38f;
        } else {
            skyR=0.05f; skyG=0.07f; skyB=0.14f; ovR=0.11f; ovG=0.12f; ovB=0.18f;
        }
        D2D1_COLOR_F bgTop = D2D1::ColorF(Lerp(skyR,ovR,cover), Lerp(skyG,ovG,cover), Lerp(skyB,ovB,cover));
        D2D1_COLOR_F bgBot = isNight
            ? D2D1::ColorF(Lerp(skyR+0.02f,ovR+0.02f,cover), Lerp(skyG+0.03f,ovG+0.02f,cover), Lerp(skyB+0.06f,ovB+0.04f,cover))
            : D2D1::ColorF(Lerp(skyR+0.20f,ovR+0.08f,cover), Lerp(skyG+0.18f,ovG+0.06f,cover), Lerp(skyB+0.08f,ovB+0.02f,cover));
        auto bg = MakeLinGrad(bgTop, bgBot);
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        FillAtmosphereLayer(T + H * 0.46f, B - H * 0.12f,
            D2D1::ColorF(0.92f, 0.94f, 0.96f, isNight ? 0.03f * alpha : 0.05f * alpha),
            D2D1::ColorF(0.92f, 0.94f, 0.96f, 0.0f));
        FillRidge(B - H * 0.34f, H * 0.032f, H * 0.016f, 1.0f, 2.0f, t * 0.04f,
            isNight ? D2D1::ColorF(0.12f, 0.14f, 0.18f) : D2D1::ColorF(0.42f, 0.48f, 0.52f), 0.38f);
        FillRidge(B - H * 0.23f, H * 0.045f, H * 0.018f, 1.7f, 3.1f, t * 0.06f,
            isNight ? D2D1::ColorF(0.06f, 0.07f, 0.10f) : D2D1::ColorF(0.24f, 0.30f, 0.26f), 0.78f);
        FillTreeLine(B - H * 0.17f, W * 0.068f, H * 0.05f, H * 0.10f,
            isNight ? D2D1::ColorF(0.03f, 0.04f, 0.05f) : D2D1::ColorF(0.12f, 0.16f, 0.12f), 0.88f, t * 0.05f);
        FillMistBand(B - H * 0.12f, 0.70f, 0.09f, sinf(t * 0.18f) * W * 0.03f,
            D2D1::ColorF(1.0f, 1.0f, 1.0f, isNight ? 0.04f : 0.07f));
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
        D2D1_COLOR_F cloudCol = isNight ? D2D1::ColorF(0.24f,0.26f,0.32f) : D2D1::ColorF(0.78f,0.80f,0.84f);
        float cloudDrift = sinf(t * 0.12f) * W * 0.03f;
        FillCloudBand(cx + cloudDrift,            T + H * 0.19f, W * (partlyCloudy ? 0.62f : 0.90f), H * 0.16f, partlyCloudy ? 0.64f : 0.84f, cloudCol);
        FillCloudBand(L + W * 0.30f + cloudDrift, T + H * 0.25f, W * (partlyCloudy ? 0.42f : 0.62f), H * 0.12f, partlyCloudy ? 0.50f : 0.68f, cloudCol);
        if (!partlyCloudy) {
            FillCloudBand(R - W * 0.18f + cloudDrift, T + H * 0.30f, W * 0.48f, H * 0.11f, 0.56f, cloudCol);
        }
        FillGroundGlow(B - H * 0.09f, 0.09f, D2D1::ColorF(0.88f, 0.90f, 0.92f), isNight ? 0.02f : 0.05f);
        break;
    }
    case WeatherType::Fog: {
        D2D1_COLOR_F fogTop = D2D1::ColorF(0.60f,0.62f,0.64f);
        D2D1_COLOR_F fogBot = D2D1::ColorF(0.78f,0.80f,0.82f);
        if (sceneTime == SceneTime::Dawn) {
            fogTop = D2D1::ColorF(0.70f, 0.68f, 0.68f);
            fogBot = D2D1::ColorF(0.86f, 0.82f, 0.78f);
        } else if (sceneTime == SceneTime::Day) {
            fogTop = D2D1::ColorF(0.66f, 0.68f, 0.70f);
            fogBot = D2D1::ColorF(0.86f, 0.88f, 0.90f);
        } else if (sceneTime == SceneTime::Dusk) {
            fogTop = D2D1::ColorF(0.56f, 0.54f, 0.56f);
            fogBot = D2D1::ColorF(0.70f, 0.66f, 0.64f);
        } else {
            fogTop = D2D1::ColorF(0.30f, 0.32f, 0.36f);
            fogBot = D2D1::ColorF(0.48f, 0.50f, 0.54f);
        }
        auto bg = MakeLinGrad(fogTop, fogBot);
        if (bg) ctx->FillRectangle(D2D1::RectF(L,T,R,B), bg.Get());
        FillSoftLight(cx, T + H * 0.24f, W * 0.48f, H * 0.18f,
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f * alpha),
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f));
        FillAtmosphereLayer(T + H * 0.42f, B - H * 0.08f,
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f * alpha),
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f));
        FillRidge(B - H * 0.32f, H * 0.024f, H * 0.012f, 0.9f, 1.8f, t * 0.03f,
            D2D1::ColorF(0.54f, 0.56f, 0.58f), 0.22f);
        FillRidge(B - H * 0.20f, H * 0.032f, H * 0.014f, 1.3f, 2.5f, t * 0.05f,
            D2D1::ColorF(0.40f, 0.42f, 0.44f), 0.28f);
        FillTreeLine(B - H * 0.16f, W * 0.072f, H * 0.04f, H * 0.08f,
            D2D1::ColorF(0.26f, 0.28f, 0.30f), 0.22f, t * 0.03f);
        FillGroundGlow(B - H * 0.08f, 0.08f, D2D1::ColorF(0.94f, 0.94f, 0.92f), 0.05f);
        for (int i = 0; i < 6; ++i) {
            float fy = T + H * (0.22f + i * 0.12f);
            float drift = sinf(t * 0.16f + i * 1.13f) * W * 0.05f;
            float widthScale = 0.86f - i * 0.06f;
            float heightScale = 0.070f - i * 0.006f;
            float layerAlpha = 0.18f - i * 0.02f;
            FillMistBand(fy, widthScale, heightScale, drift, D2D1::ColorF(1.0f, 1.0f, 1.0f, layerAlpha));
        }
        break;
    }
    }
    DrawVignette();

    ctx->PopLayer();
}

// ── DrawWeatherDaily ───────────────────────────────────────────────────────

void WeatherComponent::DrawWeatherDaily(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime) {
    if (!m_res) return;
    auto* ctx = m_res->d2dContext;
    float left = std::round(rect.left), top = std::round(rect.top), right = std::round(rect.right), bottom = std::round(rect.bottom);
    float islandWidth = right - left, islandHeight = bottom - top;
    float padding = 15.0f;
    float cardTop = std::round(top + padding), cardBottom = std::round(bottom - padding);
    auto oldTextAA = ctx->GetTextAntialiasMode();
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

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
        ctx->DrawTextLayout(D2D1::Point2F(std::round(left + padding), std::round(cardTop + (cardBottom - cardTop) / 2.0f - 8.0f)), emptyLayout.Get(), m_res->grayBrush);
        ctx->SetTextAntialiasMode(oldTextAA);
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
        float cellX = std::round(left + padding + 10.0f + i * cellW);

        m_res->grayBrush->SetOpacity(contentAlpha * 0.8f);
        auto dateLayout = GetOrCreateTextLayout(df.date, m_res->subFormat, cellW, L"df_date_" + df.date + std::to_wstring(i));
        dateLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(cellX, std::round(dataTop)), dateLayout.Get(), m_res->grayBrush);

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
        ctx->DrawTextLayout(D2D1::Point2F(cellX, std::round(dataTop + 42.0f)), maxLayout.Get(), m_res->whiteBrush);

        std::wstring minText = std::to_wstring((int)df.tempMin) + L"\u00B0";
        m_res->grayBrush->SetOpacity(contentAlpha * 0.7f);
        auto minLayout = GetOrCreateTextLayout(minText, m_res->subFormat, cellW, L"df_min_" + minText + std::to_wstring(i));
        minLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(cellX, std::round(dataTop + 60.0f)), minLayout.Get(), m_res->grayBrush);
    }

    std::wstring hint = L"↑ 逐小时";
    m_res->grayBrush->SetOpacity(contentAlpha * 0.45f);
    ComPtr<IDWriteTextFormat> hintFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"zh-cn", &hintFmt);
    ctx->DrawTextW(hint.c_str(), (UINT32)hint.size(), hintFmt.Get(),
        D2D1::RectF(right-padding-60.0f, cardBottom-14.0f, right-padding, cardBottom), m_res->grayBrush);
    ctx->SetTextAntialiasMode(oldTextAA);
}

// ── DrawWeatherExpanded ────────────────────────────────────────────────────

void WeatherComponent::DrawWeatherExpanded(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime) {
    if (!m_res) return;
    auto* ctx = m_res->d2dContext;
    auto* fac = m_res->d2dFactory;
    float left = std::round(rect.left);
    float top = std::round(rect.top);
    float right = std::round(rect.right);
    float bottom = std::round(rect.bottom);
    float islandWidth = right - left;
    float padding = 15.0f;
    float cardTop = std::round(top + padding);
    float cardBottom = std::round(bottom - padding);
    float cardWidth = std::round((islandWidth - padding * 3) / 2.0f);
    float leftCardLeft = std::round(left + padding);
    float rightCardLeft = std::round(leftCardLeft + cardWidth + padding);

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

    std::wstring tempText = std::to_wstring((int)m_temp) + L"\u00B0";
    ComPtr<IDWriteTextFormat> hugeTempFormat;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 34.0f, L"zh-cn", &hugeTempFormat);
    hugeTempFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    hugeTempFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    ComPtr<IDWriteTextFormat> descFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"zh-cn", &descFmt);
    descFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    descFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    ComPtr<IDWriteTextFormat> locationFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"zh-cn", &locationFmt);
    locationFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    locationFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    const float textWidth = (std::max)(60.0f, cardWidth - 20.0f);
    const float textLeft = std::round(leftCardLeft + (cardWidth - textWidth) * 0.5f);

    ComPtr<IDWriteTextLayout> tempLayout;
    ComPtr<IDWriteTextLayout> descLayout;
    ComPtr<IDWriteTextLayout> locationLayout;
    m_res->dwriteFactory->CreateTextLayout(tempText.c_str(), (UINT32)tempText.length(), hugeTempFormat.Get(), cardWidth, 80.0f, &tempLayout);
    m_res->dwriteFactory->CreateTextLayout(m_desc.c_str(), (UINT32)m_desc.length(), descFmt.Get(), textWidth, 30.0f, &descLayout);
    const std::wstring locationText = m_locationText.empty() ? L"北京" : m_locationText;
    m_res->dwriteFactory->CreateTextLayout(locationText.c_str(), (UINT32)locationText.length(), locationFmt.Get(), textWidth, 24.0f, &locationLayout);

    ComPtr<IDWriteInlineObject> trimmingSign;
    m_res->dwriteFactory->CreateEllipsisTrimmingSign(locationFmt.Get(), &trimmingSign);
    DWRITE_TRIMMING trimming = {};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    if (locationLayout && trimmingSign) {
        locationLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        locationLayout->SetTrimming(&trimming, trimmingSign.Get());
    }

    DWRITE_TEXT_METRICS tempMetrics{};
    DWRITE_TEXT_METRICS descMetrics{};
    DWRITE_TEXT_METRICS locationMetrics{};
    if (tempLayout) {
        tempLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        tempLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        tempLayout->GetMetrics(&tempMetrics);
    }
    if (descLayout) {
        descLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        descLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        descLayout->GetMetrics(&descMetrics);
    }
    if (locationLayout) {
        locationLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        locationLayout->GetMetrics(&locationMetrics);
    }

    const float bottomPadding = 16.0f;
    const float lineGap = 4.0f;
    const float tempGap = 8.0f;
    float descY = std::round(cardBottom - bottomPadding - descMetrics.height);
    float locationY = std::round(descY - lineGap - locationMetrics.height);
    float tempY = std::round(locationY - tempGap - tempMetrics.height);

    auto oldTextAA = ctx->GetTextAntialiasMode();
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(tempText.c_str(), (UINT32)tempText.length(), hugeTempFormat.Get(),
        D2D1::RectF(leftCardLeft, tempY, leftCardLeft + cardWidth, tempY + std::round(tempMetrics.height + 6.0f)),
        m_res->whiteBrush);

    m_res->grayBrush->SetOpacity(contentAlpha * 0.78f);
    if (locationLayout) {
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, locationY), locationLayout.Get(), m_res->grayBrush);
    } else {
        ctx->DrawTextW(locationText.c_str(), (UINT32)locationText.length(), locationFmt.Get(),
            D2D1::RectF(textLeft, locationY, textLeft + textWidth, locationY + std::round(locationMetrics.height + 4.0f)),
            m_res->grayBrush);
    }

    m_res->grayBrush->SetOpacity(contentAlpha * 0.60f);
    if (descLayout) {
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, descY), descLayout.Get(), m_res->grayBrush);
    } else {
        ctx->DrawTextW(m_desc.c_str(), (UINT32)m_desc.length(), descFmt.Get(),
            D2D1::RectF(textLeft, descY, textLeft + textWidth, descY + std::round(descMetrics.height + 4.0f)),
            m_res->grayBrush);
    }
    ctx->SetTextAntialiasMode(oldTextAA);

    // 右侧 2×3 逐小时网格
    if (!m_hourly.empty()) {
        int rows=2, cols=3;
        float cellWidth  = cardWidth / cols;
        float cellHeight = (cardBottom - cardTop) / rows;
        for (size_t i = 0; i < m_hourly.size() && i < 6; ++i) {
            int row = (int)(i / cols), col = (int)(i % cols);
            float cellX = std::round(rightCardLeft + col * cellWidth);
            float cellY = std::round(cardTop + row * cellHeight);

            m_res->grayBrush->SetOpacity(contentAlpha * 0.75f);
            auto timeLayout = GetOrCreateTextLayout(m_hourly[i].time, m_res->subFormat, cellWidth, L"hf_time_" + m_hourly[i].time);
            timeLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ctx->DrawTextLayout(D2D1::Point2F(cellX, std::round(cellY + 6.0f)), timeLayout.Get(), m_res->grayBrush);

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
            ctx->DrawTextLayout(D2D1::Point2F(cellX, std::round(cellY + 44.0f)), tempLayout.Get(), m_res->whiteBrush);
        }
    } else {
        std::wstring emptyText = L"暂无预报数据";
        m_res->grayBrush->SetOpacity(contentAlpha);
        auto emptyLayout = GetOrCreateTextLayout(emptyText, m_res->subFormat, cardWidth, L"hf_empty");
        emptyLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        ctx->DrawTextLayout(D2D1::Point2F(rightCardLeft, std::round(cardTop + (cardBottom - cardTop) / 2.0f - 10.0f)), emptyLayout.Get(), m_res->grayBrush);
    }

    // 底部提示
    std::wstring hint = L"↓ 逐日预报";
    m_res->grayBrush->SetOpacity(contentAlpha * 0.45f);
    ComPtr<IDWriteTextFormat> hintFmt;
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"zh-cn", &hintFmt);
    ctx->DrawTextW(hint.c_str(), (UINT32)hint.size(), hintFmt.Get(),
        D2D1::RectF(std::round(right-padding-70.0f), std::round(bottom-padding-14.0f), std::round(right-padding), std::round(bottom-padding)), m_res->grayBrush);
}
