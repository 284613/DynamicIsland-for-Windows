#pragma once
#include "IIslandComponent.h"
#include "IslandState.h"
#include "PluginManager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

enum class WeatherType {
    Clear,
    PartlyCloudy,
    Cloudy,
    Rainy,
    Thunder,
    Snow,
    Fog,
    Default
};

class WeatherComponent : public IIslandComponent {
public:
    WeatherComponent() = default;
    ~WeatherComponent() = default;

    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_isExpanded; }
    bool NeedsRender() const override { return m_isExpanded; }
    bool OnMouseWheel(float x, float y, int delta) override;

    void SetWeatherData(
        const std::wstring& locationText,
        const std::wstring& desc,
        float temp,
        const std::wstring& iconId,
        const std::vector<HourlyForecast>& hourly,
        const std::vector<DailyForecast>& daily);

    void SetExpanded(bool expanded) { m_isExpanded = expanded; }
    void SetViewMode(WeatherViewMode mode);
    WeatherViewMode GetViewMode() const { return m_viewMode; }

    void DrawCompact(float iconX, float iconY, float iconSize,
                     float contentAlpha, ULONGLONG currentTimeMs);

    void ResetAnimation() { m_animPhase = 0.0f; m_lastAnimTime = 0; }

private:
    void DrawWeatherExpanded(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime);
    void DrawWeatherDaily(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime);
    void DrawWeatherView(WeatherViewMode mode, const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime);
    void DrawWeatherAmbientBg(float L, float T, float R, float B, float alpha, ULONGLONG currentTime);
    void DrawWeatherIcon(float x, float y, float size, float alpha, ULONGLONG currentTime);
    WeatherType MapWeatherDescToType(const std::wstring& desc) const;
    void BeginViewTransition(WeatherViewMode targetMode);
    float EaseOutCubic(float t) const;

    void DrawCloud(float cx, float cy, float s, float op, ID2D1Brush* brush);
    void DrawLine(float x1, float y1, float x2, float y2, ID2D1Brush* brush, float w);

    ComPtr<IDWriteTextLayout> GetOrCreateTextLayout(
        const std::wstring& text, IDWriteTextFormat* fmt,
        float maxWidth, const std::wstring& cacheKey);

    SharedResources* m_res = nullptr;

    std::wstring m_locationText;
    std::wstring m_desc;
    float m_temp = 0.0f;
    std::wstring m_iconId;
    std::vector<HourlyForecast> m_hourly;
    std::vector<DailyForecast> m_daily;

    bool m_isExpanded = false;
    WeatherViewMode m_viewMode = WeatherViewMode::Hourly;
    WeatherViewMode m_targetViewMode = WeatherViewMode::Hourly;
    WeatherViewMode m_transitionFromMode = WeatherViewMode::Hourly;
    bool m_isViewTransitioning = false;
    float m_viewTransitionProgress = 1.0f;
    float m_viewTransitionDuration = 0.20f;
    WeatherType m_weatherType = WeatherType::Default;
    float m_animPhase = 0.0f;
    ULONGLONG m_lastAnimTime = 0;

    struct CacheEntry {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        float maxWidth = 0.0f;
    };
    std::unordered_map<std::wstring, CacheEntry> m_layoutCache;
};
