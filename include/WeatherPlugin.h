#pragma once
#include "PluginManager.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
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
    std::wstring GetLocationText() const override;
    std::wstring GetWeatherDescription() const override;
    float GetTemperature() const override;
    std::wstring GetIconId() const override;
    std::vector<HourlyForecast> GetHourlyForecast() const override;
    std::vector<DailyForecast> GetDailyForecast() const override;
    std::wstring GetLifeSuggestion() const override;
    bool HasSevereWarning() const override;

    // 强制下次 Update 时立即刷新（无视时间间隔）
    void RequestRefresh() { m_lastUpdateTime = 0; }
    // 设置 fetch 完成后的通知窗口
    void SetNotifyHwnd(HWND hwnd) { m_notifyHwnd = hwnd; }
    bool IsAvailable() const;

private:
    void FetchWeather();
    void JoinFetchThread();
    void SetUnavailableState(const std::wstring& locationText, const std::wstring& reason);

private:
    mutable std::mutex m_stateMutex;
    std::wstring m_locationText;
    std::wstring m_description;
    float m_temperature = 0.0f;
    std::wstring m_iconId = L"100";
    std::vector<HourlyForecast> m_hourlyForecasts;
    std::vector<DailyForecast> m_dailyForecasts;
    std::wstring m_lifeSuggestion;
    bool m_hasSevereWarning = false;
    bool m_isAvailable = true;

    size_t m_lastUpdateTime = 0;
    HWND   m_notifyHwnd = nullptr;
    std::thread m_fetchThread;
    std::atomic<bool> m_fetchInProgress{ false };
    std::atomic<bool> m_shutdownRequested{ false };
};


