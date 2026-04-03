#include "SystemMonitor.h"
#include "Messages.h"
#include "EventBus.h"
#include <string>
#include <windows.h>      // [必须添加] 包含 Windows 基础 API
#include <powrprof.h>     // [必须添加] 包含 PBT_POWERSETTINGCHANGE 等常量

// [可选] 定义标准电源插拔监听 GUID (如果系统头文件里没自带的话)
#ifndef GUID_ACDC_POWER_SOURCE
DEFINE_GUID(GUID_ACDC_POWER_SOURCE, 0x5d3e9a54, 0x29d6, 0x453e, 0xb0, 0x25, 0x13, 0xA7, 0x84, 0xEC, 0x33, 0x2B);
#endif

SystemMonitor::SystemMonitor()
    : m_hwnd(nullptr), m_powerHandle(nullptr), m_lastACStatus(0), m_lastBatteryPct(0), m_lowBatteryAlerted(false), m_weatherPlugin(nullptr) {
    // [建议] 在构造函数里初始化成员变量，避免随机值
    m_weatherPlugin = new WeatherPlugin();
    if (m_weatherPlugin) {
        m_weatherPlugin->Initialize();
    }
}

SystemMonitor::~SystemMonitor() {
    if (m_powerHandle) {
        UnregisterPowerSettingNotification(m_powerHandle);
        m_powerHandle = nullptr;
    }
    if (m_weatherPlugin) {
        m_weatherPlugin->Shutdown();
        delete m_weatherPlugin;
        m_weatherPlugin = nullptr;
    }
}

void SystemMonitor::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    // 初始获取状态
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        m_lastACStatus = sps.ACLineStatus;
        m_lastBatteryPct = sps.BatteryLifePercent;
    }

    // 注册电源设置通知
    // [优化] 使用 GUID_ACDC_POWER_SOURCE 专门监听电源插拔，反应更及时
    m_powerHandle = RegisterPowerSettingNotification(
        hwnd,
        &GUID_ACDC_POWER_SOURCE,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    if (!m_powerHandle) {
        OutputDebugStringW(L"SystemMonitor: Failed to register power notification\n");
    }
}

void SystemMonitor::OnPowerEvent(WPARAM wParam, LPARAM lParam) {
    // 处理电源设置变化事件
    if (wParam == PBT_POWERSETTINGCHANGE) {
        SYSTEM_POWER_STATUS sps;
        if (GetSystemPowerStatus(&sps)) {
            if (sps.BatteryLifePercent != 255) {
                std::wstring pctStr = std::to_wstring(sps.BatteryLifePercent) + L"%";

                // 1. 检查是否刚刚插上电源
                if (sps.ACLineStatus == 1 && m_lastACStatus == 0) {
                    AlertInfo info{ 4, L"电源已连接", pctStr, L"", nullptr };
                    EventBus::GetInstance().PublishNotificationArrived(info);
                    m_lowBatteryAlerted = false;
                }
                // 2. 检查是否刚刚拔下电源
                else if (sps.ACLineStatus == 0 && m_lastACStatus == 1) {
                    AlertInfo info{ 4, L"正在使用电池", pctStr, L"", nullptr };
                    EventBus::GetInstance().PublishNotificationArrived(info);
                }

                // 3. 低电量检查
                if (sps.ACLineStatus == 0 && sps.BatteryLifePercent <= 20 && !m_lowBatteryAlerted) {
                    AlertInfo info{ 5, L"电量不足 20%", L"请连接电源", L"", nullptr };
                    EventBus::GetInstance().PublishNotificationArrived(info);
                    m_lowBatteryAlerted = true;
                }

                m_lastACStatus = sps.ACLineStatus;
                m_lastBatteryPct = sps.BatteryLifePercent;
            }
        }
    }
}

