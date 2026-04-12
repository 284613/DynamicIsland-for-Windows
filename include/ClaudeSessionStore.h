#pragma once

#include "AgentSessionModel.h"
#include "ClaudeHookBridge.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ClaudeSessionStore {
public:
    void ProcessEvent(const ClaudeHookEvent& event);
    void MergeHistoryFallback(const std::vector<AgentSessionSummary>& fallbackSummaries);
    std::vector<AgentSessionSummary> GetSummaries() const;
    bool RespondToPermission(class ClaudeHookBridge& bridge, const std::wstring& toolUseId, const std::wstring& decision, const std::wstring& reason = L"");

private:
    struct ClaudeRuntimeSession {
        std::wstring sessionId;
        std::wstring projectPath;
        std::wstring title;
        uint64_t lastActivityTs = 0;
        AgentSessionPhase phase = AgentSessionPhase::Idle;
        std::wstring statusText;
        std::wstring pendingToolName;
        std::wstring pendingToolUseId;
        std::wstring recentActivityText;
    };

    void UpdateSessionFromEvent(ClaudeRuntimeSession& session, const ClaudeHookEvent& event);
    static AgentSessionPhase PhaseFromEvent(const ClaudeHookEvent& event);
    static std::wstring DefaultStatusText(AgentSessionPhase phase);

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, ClaudeRuntimeSession> m_sessions;
};
