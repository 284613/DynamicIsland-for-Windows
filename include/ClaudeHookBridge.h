#pragma once

#include "AgentSessionModel.h"
#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

struct ClaudeHookEvent {
    std::wstring sessionId;
    std::wstring cwd;
    std::wstring event;
    std::wstring status;
    std::wstring tool;
    std::wstring toolInputSummary;
    std::wstring toolUseId;
    std::wstring notificationType;
    std::wstring message;
    int pid = 0;
    uint64_t receivedAtMs = 0;
};

class ClaudeHookBridge {
public:
    using HookEventHandler = std::function<void(const ClaudeHookEvent&)>;

    ClaudeHookBridge();
    ~ClaudeHookBridge();

    bool Start(HookEventHandler handler);
    void Stop();
    bool RespondToPermission(const std::wstring& toolUseId, const std::wstring& decision, const std::wstring& reason = L"");

    bool IsRunning() const { return m_running.load(); }
    static bool IsListenerRunning();
    static const wchar_t* PipeName();

private:
    struct PendingPermission {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::wstring sessionId;
        std::wstring toolUseId;
    };

    void WorkerLoop();
    void HandleClient(HANDLE pipeHandle);
    bool ReadPipeMessage(HANDLE pipeHandle, std::string& message) const;
    void ClosePipeHandle(HANDLE pipeHandle) const;
    std::wstring BuildCacheKey(const ClaudeHookEvent& event) const;
    void CacheToolUseId(const ClaudeHookEvent& event);
    void PruneToolCache(const std::wstring& sessionId);
    void CompletePendingPermission(const std::wstring& toolUseId);

    HookEventHandler m_handler;
    std::thread m_worker;
    std::atomic<bool> m_running{ false };
    std::mutex m_pendingMutex;
    std::unordered_map<std::wstring, PendingPermission> m_pendingPermissions;
    std::mutex m_cacheMutex;
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_toolUseIdCache;

    static std::atomic<bool> s_listenerRunning;
};
