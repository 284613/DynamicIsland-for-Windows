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

    // 强制下次 Update 时立即刷新（无视时间间隔）
    void RequestRefresh() { m_lastUpdateTime = 0; }

private:
    void FetchWeather();

private:
    std::wstring m_description;
    float m_temperature = 0.0f;
    size_t m_lastUpdateTime = 0;
};


