#pragma once

#include "AgentSessionModel.h"
#include "CodexHookBridge.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CodexSessionStore {
public:
    void ProcessEvent(const CodexHookEvent& event);
    void MergeHistoryFallback(const std::vector<AgentSessionSummary>& fallbackSummaries);
    std::vector<AgentSessionSummary> GetSummaries() const;

private:
    struct CodexRuntimeSession {
        std::wstring sessionId;
        std::wstring projectPath;
        std::wstring title;
        uint64_t lastActivityTs = 0;
        AgentSessionPhase phase = AgentSessionPhase::Idle;
        std::wstring statusText;
        std::wstring recentActivityText;
        std::wstring activeToolUseId;
    };

    void UpdateSessionFromEvent(CodexRuntimeSession& session, const CodexHookEvent& event);
    static AgentSessionPhase PhaseFromEvent(const CodexHookEvent& event);
    static std::wstring DefaultStatusText(AgentSessionPhase phase);

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, CodexRuntimeSession> m_sessions;
};
