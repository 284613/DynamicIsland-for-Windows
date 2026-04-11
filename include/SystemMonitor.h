#pragma once
#include <windows.h>
#include <powrprof.h>
#include <string>
#include "WeatherPlugin.h"

class SystemMonitor 
{
public:
    SystemMonitor();
    ~SystemMonitor();
    void Initialize(HWND hwnd);
    // 处理电源事件（供主窗口调用）
    void OnPowerEvent(WPARAM wParam, LPARAM lParam);
    void SetLowBatteryThreshold(int threshold) { m_lowBatteryThreshold = threshold; }

    // 天气相关
    std::wstring GetWeatherDescription() const { return m_weatherDesc; }
    float GetWeatherTemperature() const { return m_weatherTemp; }
    WeatherPlugin* GetWeatherPlugin() const { return m_weatherPlugin; }
    void UpdateWeather() {
        if (m_weatherPlugin) m_weatherPlugin->Update(0.0f);
        if (m_weatherPlugin) {
            m_weatherDesc = m_weatherPlugin->GetWeatherDescription();
            m_weatherTemp = m_weatherPlugin->GetTemperature();
        }
    }

private:
    HWND m_hwnd = nullptr;
    HPOWERNOTIFY m_powerHandle = nullptr; // 电源通知句柄

    BYTE m_lastACStatus = 255;   // 0=电池, 1=交流电源, 255=未知
    BYTE m_lastBatteryPct = 255; // 电池百分比
    bool m_lowBatteryAlerted = false; // 是否已经报过低电量
    int m_lowBatteryThreshold = 20;

    // 天气插件
    WeatherPlugin* m_weatherPlugin = nullptr;
    std::wstring m_weatherDesc = L"Sunny";
    float m_weatherTemp = 25.0f;
};


