#pragma once
#include "PluginManager.h"
#include <string>
#include <windows.h>
#include <winhttp.h>

class WeatherPlugin : public IWeatherPlugin {
public:
    WeatherPlugin();
    ~WeatherPlugin();

    // IPlugin implementation
    PluginInfo GetInfo() const override;
    bool Initialize() override;
    void Shutdown() override;
    void Update(float deltaTime) override;

    // IWeatherPlugin implementation
    std::wstring GetWeatherDescription() const override { return m_description; }
    float GetTemperature() const override { return m_temperature; }
    std::wstring GetIconId() const override { return m_iconId; }
    std::vector<HourlyForecast> GetHourlyForecast() const override { return m_hourlyForecasts; }
    std::wstring GetLifeSuggestion() const override { return m_lifeSuggestion; }
    bool HasSevereWarning() const override { return m_hasSevereWarning; }

    // 强制下次 Update 时立即刷新（无视时间间隔）
    void RequestRefresh() { m_lastUpdateTime = 0; }

private:
    void FetchWeather();

private:
    std::wstring m_description;
    float m_temperature = 0.0f;
    std::wstring m_iconId = L"100";
    std::vector<HourlyForecast> m_hourlyForecasts;
    std::wstring m_lifeSuggestion;
    bool m_hasSevereWarning = false;

    size_t m_lastUpdateTime = 0;
};


