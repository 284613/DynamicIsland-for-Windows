#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <windows.h>

class FaceUnlockBridge {
public:
    FaceUnlockBridge() = default;
    ~FaceUnlockBridge();

    FaceUnlockBridge(const FaceUnlockBridge&) = delete;
    FaceUnlockBridge& operator=(const FaceUnlockBridge&) = delete;

    bool Start(HWND notifyHwnd);
    void Stop();

private:
    void WorkerLoop();
    void PostEventFromJson(const std::string& json);

    HWND m_notifyHwnd = nullptr;
    std::atomic<bool> m_stop{ false };
    std::thread m_worker;
};
