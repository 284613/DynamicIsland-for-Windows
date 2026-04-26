#pragma once

#include "AgentSessionModel.h"
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class AgentSessionMonitor {
public:
    AgentSessionMonitor();
    ~AgentSessionMonitor();

    bool Initialize(HWND targetHwnd);
    void Shutdown();
    void RequestRefresh();

    std::vector<AgentSessionSummary> GetSummaries() const;
    std::vector<AgentHistoryEntry> GetHistory(AgentKind kind, const std::wstring& sessionId) const;

    struct ClaudeHudSnapshot {
        std::wstring cwd;
        std::wstring modelName;
        int contextUsedPercent = -1;
        int contextRemainingPercent = -1;
        uint64_t inputTokens = 0;
        uint64_t outputTokens = 0;
        uint64_t cacheCreationTokens = 0;
        uint64_t cacheReadTokens = 0;
        int fiveHourUsedPercent = -1;
        int sevenDayUsedPercent = -1;
        uint64_t fiveHourResetAtMs = 0;
        uint64_t sevenDayResetAtMs = 0;
    };

    struct SessionRecord {
        AgentKind kind = AgentKind::Claude;
        std::wstring sessionId;
        std::wstring projectPath;
        std::wstring title;
        uint64_t lastActivityTs = 0;
        bool hasRichHistory = false;
        bool hasHudData = false;
        std::wstring modelName;
        int contextUsedPercent = -1;
        int contextRemainingPercent = -1;
        uint64_t inputTokens = 0;
        uint64_t outputTokens = 0;
        uint64_t cacheCreationTokens = 0;
        uint64_t cacheReadTokens = 0;
        int fiveHourUsedPercent = -1;
        int sevenDayUsedPercent = -1;
        uint64_t fiveHourResetAtMs = 0;
        uint64_t sevenDayResetAtMs = 0;
        std::vector<AgentHistoryEntry> history;
    };

    struct FileStamp {
        uintmax_t size = 0;
        long long writeTicks = 0;

        bool operator==(const FileStamp& other) const {
            return size == other.size && writeTicks == other.writeTicks;
        }
    };

private:
    void WorkerLoop();
    bool RefreshSources();
    bool RefreshLiveStateOnly();
    void RebuildSnapshotsLocked();

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, SessionRecord> m_records;
    std::vector<AgentSessionSummary> m_summaries;
    std::unordered_map<std::wstring, FileStamp> m_fileStamps;
    ClaudeHudSnapshot m_claudeHudSnapshot;

    HWND m_targetHwnd = nullptr;
    std::thread m_worker;
    std::mutex m_waitMutex;
    std::condition_variable m_waitCv;
    bool m_running = false;
    bool m_refreshRequested = false;
};
