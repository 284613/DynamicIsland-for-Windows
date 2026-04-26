//NotificationMonitor.h
#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <deque>
#include <string>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <winrt/Windows.Storage.Streams.h>

// Extract icon path for app notifications (QQ/WeChat) - defined in DynamicIsland.cpp
std::wstring ExtractIconFromExe(const std::wstring& appName);

class NotificationMonitor {
public:
    NotificationMonitor();
    ~NotificationMonitor();
    void Initialize(HWND hwnd, const std::vector<std::wstring>& allowedApps);
    void UpdateAllowedApps(const std::vector<std::wstring>& allowedApps);

private:
    void Worker();
    std::vector<uint8_t> ReadIconToMemory(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& icon);
    bool MarkProcessed(uint32_t notificationId);

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    HWND m_hwnd = nullptr;
    std::vector<std::wstring> m_allowedApps;
    std::mutex m_allowedAppsMutex;
    std::unordered_set<uint32_t> m_processedNotifSet;
    std::deque<uint32_t> m_processedNotifOrder;

    static constexpr size_t kProcessedNotificationLimit = 512;
};

