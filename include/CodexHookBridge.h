#pragma once

#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

struct CodexHookEvent {
    std::wstring sessionId;
    std::wstring cwd;
    std::wstring turnId;
    std::wstring event;
    std::wstring tool;
    std::wstring toolInputSummary;
    std::wstring toolUseId;
    std::wstring permissionMode;
    std::wstring source;
    std::wstring message;
    uint64_t receivedAtMs = 0;
};

class CodexHookBridge {
public:
    using HookEventHandler = std::function<void(const CodexHookEvent&)>;

    CodexHookBridge();
    ~CodexHookBridge();

    bool Start(HookEventHandler handler);
    void Stop();

    bool IsRunning() const { return m_running.load(); }
    static bool IsListenerRunning();
    static const wchar_t* PipeName();

private:
    void WorkerLoop();
    void HandleClient(HANDLE pipeHandle);
    bool ReadPipeMessage(HANDLE pipeHandle, std::string& message) const;
    void ClosePipeHandle(HANDLE pipeHandle) const;

    HookEventHandler m_handler;
    std::thread m_worker;
    std::atomic<bool> m_running{ false };

    static std::atomic<bool> s_listenerRunning;
};
