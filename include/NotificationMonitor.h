//NotificationMonitor.h
#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <winrt/Windows.Storage.Streams.h>

// Extract icon path for app notifications (QQ/WeChat) - defined in DynamicIsland.cpp
std::wstring ExtractIconFromExe(const std::wstring& appName);

class NotificationMonitor {
public:
    NotificationMonitor();
    ~NotificationMonitor();
    void Initialize(HWND hwnd, const std::vector<std::wstring>& allowedApps);

private:
    void Worker();
    std::vector<uint8_t>* ReadIconToMemory(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& icon);

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    HWND m_hwnd = nullptr;
    std::vector<std::wstring> m_allowedApps;
    std::set<uint32_t> m_processedNotifs; // 记录已处理的通知，防止重复弹
};

