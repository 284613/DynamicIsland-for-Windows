#include "ClaudeSessionStore.h"

#include <algorithm>
#include <filesystem>

namespace {
uint64_t MaxTs(uint64_t lhs, uint64_t rhs) {
    return lhs > rhs ? lhs : rhs;
}
}

void ClaudeSessionStore::ProcessEvent(const ClaudeHookEvent& event) {
    if (event.sessionId.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (event.event == L"SessionEnd" || event.status == L"ended") {
        m_sessions.erase(event.sessionId);
        return;
    }

    ClaudeRuntimeSession& session = m_sessions[event.sessionId];
    session.sessionId = event.sessionId;
    UpdateSessionFromEvent(session, event);
}

void ClaudeSessionStore::MergeHistoryFallback(const std::vector<AgentSessionSummary>& fallbackSummaries) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& summary : fallbackSummaries) {
        if (summary.kind != AgentKind::Claude) {
            continue;
        }

        ClaudeRuntimeSession& session = m_sessions[summary.sessionId];
        session.sessionId = summary.sessionId;
        if (session.projectPath.empty()) {
            session.projectPath = summary.projectPath;
        }
        if (session.title.empty()) {
            session.title = summary.title;
        }
        session.lastActivityTs = MaxTs(session.lastActivityTs, summary.lastActivityTs);
        if (session.statusText.empty()) {
            session.statusText = summary.isLive ? L"Working" : L"Idle";
        }
    }
}

std::vector<AgentSessionSummary> ClaudeSessionStore::GetSummaries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AgentSessionSummary> summaries;
    summaries.reserve(m_sessions.size());

    for (const auto& pair : m_sessions) {
        const ClaudeRuntimeSession& session = pair.second;
        AgentSessionSummary summary;
        summary.kind = AgentKind::Claude;
        summary.sessionId = session.sessionId;
        summary.projectPath = session.projectPath;
        summary.title = session.title;
        summary.lastActivityTs = session.lastActivityTs;
        summary.phase = session.phase;
        summary.isLive = session.phase == AgentSessionPhase::Processing || session.phase == AgentSessionPhase::Compacting;
        summary.statusText = session.statusText;
        summary.pendingToolName = session.pendingToolName;
        summary.pendingToolUseId = session.pendingToolUseId;
        summary.providerSupportsHooks = true;
        summary.recentActivityText = session.recentActivityText;
        summaries.push_back(std::move(summary));
    }

    std::sort(summaries.begin(), summaries.end(), [](const AgentSessionSummary& lhs, const AgentSessionSummary& rhs) {
        if (lhs.lastActivityTs != rhs.lastActivityTs) {
            return lhs.lastActivityTs > rhs.lastActivityTs;
        }
        return lhs.sessionId < rhs.sessionId;
    });
    return summaries;
}

bool ClaudeSessionStore::RespondToPermission(ClaudeHookBridge& bridge, const std::wstring& toolUseId, const std::wstring& decision, const std::wstring& reason) {
    if (toolUseId.empty() || !bridge.RespondToPermission(toolUseId, decision, reason)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_sessions) {
        ClaudeRuntimeSession& session = pair.second;
        if (session.pendingToolUseId == toolUseId) {
            session.pendingToolUseId.clear();
            session.pendingToolName.clear();
            session.phase = AgentSessionPhase::Processing;
            session.statusText = L"Processing";
            break;
        }
    }
    return true;
}

void ClaudeSessionStore::UpdateSessionFromEvent(ClaudeRuntimeSession& session, const ClaudeHookEvent& event) {
    if (session.projectPath.empty()) {
        session.projectPath = event.cwd;
    }
    session.lastActivityTs = MaxTs(session.lastActivityTs, event.receivedAtMs);
    session.phase = PhaseFromEvent(event);
    session.statusText = DefaultStatusText(session.phase);

    if (!event.tool.empty()) {
        session.recentActivityText = event.tool;
    } else if (!event.message.empty()) {
        session.recentActivityText = event.message;
    } else if (!event.event.empty()) {
        session.recentActivityText = event.event;
    }

    if (!event.message.empty()) {
        session.title = event.message;
    } else if (!event.tool.empty()) {
        session.title = event.tool;
    } else if (!event.cwd.empty() && session.title.empty()) {
        session.title = std::filesystem::path(event.cwd).filename().wstring();
    }

    if (session.phase == AgentSessionPhase::WaitingForApproval) {
        session.pendingToolName = event.tool;
        session.pendingToolUseId = event.toolUseId;
        if (!event.tool.empty()) {
            session.statusText = L"Waiting for approval: " + event.tool;
        }
    } else if (session.phase != AgentSessionPhase::Processing && session.phase != AgentSessionPhase::Compacting) {
        session.pendingToolName.clear();
        session.pendingToolUseId.clear();
    }
}

AgentSessionPhase ClaudeSessionStore::PhaseFromEvent(const ClaudeHookEvent& event) {
    if (event.event == L"PermissionRequest" || event.status == L"waiting_for_approval") {
        return AgentSessionPhase::WaitingForApproval;
    }
    if ((event.event == L"Notification" && event.notificationType == L"idle_prompt") ||
        event.event == L"Stop" || event.event == L"SubagentStop" || event.event == L"SessionStart" ||
        event.status == L"waiting_for_input") {
        return AgentSessionPhase::WaitingForInput;
    }
    if (event.event == L"PreCompact" || event.status == L"compacting") {
        return AgentSessionPhase::Compacting;
    }
    if (event.event == L"PreToolUse" || event.event == L"PostToolUse" ||
        event.status == L"running_tool" || event.status == L"processing" || event.status == L"starting") {
        return AgentSessionPhase::Processing;
    }
    if (event.event == L"SessionEnd" || event.status == L"ended") {
        return AgentSessionPhase::Ended;
    }
    return AgentSessionPhase::Idle;
}

std::wstring ClaudeSessionStore::DefaultStatusText(AgentSessionPhase phase) {
    switch (phase) {
    case AgentSessionPhase::Processing:
        return L"Processing";
    case AgentSessionPhase::WaitingForApproval:
        return L"Waiting for approval";
    case AgentSessionPhase::WaitingForInput:
        return L"Waiting for input";
    case AgentSessionPhase::Compacting:
        return L"Compacting";
    case AgentSessionPhase::Ended:
        return L"Ended";
    case AgentSessionPhase::Idle:
    default:
        return L"Idle";
    }
}
