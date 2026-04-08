//ConnectionMonitor.h
#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include <set>

class ConnectionMonitor {
public:
    ConnectionMonitor();
    ~ConnectionMonitor();
    void Initialize(HWND hwnd);

private:
    void Worker();

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    HWND m_hwnd = nullptr;

    std::wstring m_lastWifiName = L"";
    std::set<std::wstring> m_connectedBtDevices;
};


