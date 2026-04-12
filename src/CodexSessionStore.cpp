#include "CodexSessionStore.h"

#include <algorithm>
#include <filesystem>

namespace {
uint64_t MaxTs(uint64_t lhs, uint64_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

std::wstring CollapseWhitespace(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());

    bool lastWasSpace = false;
    for (wchar_t ch : text) {
        const bool isSpace = (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r');
        if (isSpace) {
            if (!result.empty() && !lastWasSpace) {
                result.push_back(L' ');
            }
            lastWasSpace = true;
            continue;
        }

        result.push_back(ch);
        lastWasSpace = false;
    }

    while (!result.empty() && result.front() == L' ') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == L' ') {
        result.pop_back();
    }
    return result;
}

std::wstring MakeExcerpt(const std::wstring& text, size_t maxLength = 96) {
    std::wstring collapsed = CollapseWhitespace(text);
    if (collapsed.size() <= maxLength) {
        return collapsed;
    }
    return collapsed.substr(0, maxLength - 1) + L"\x2026";
}
}

void CodexSessionStore::ProcessEvent(const CodexHookEvent& event) {
    if (event.sessionId.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    CodexRuntimeSession& session = m_sessions[event.sessionId];
    session.sessionId = event.sessionId;
    UpdateSessionFromEvent(session, event);
}

void CodexSessionStore::MergeHistoryFallback(const std::vector<AgentSessionSummary>& fallbackSummaries) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& summary : fallbackSummaries) {
        if (summary.kind != AgentKind::Codex) {
            continue;
        }

        CodexRuntimeSession& session = m_sessions[summary.sessionId];
        session.sessionId = summary.sessionId;
        if (session.projectPath.empty()) {
            session.projectPath = summary.projectPath;
        }
        if (session.title.empty()) {
            session.title = summary.title;
        }
        session.lastActivityTs = MaxTs(session.lastActivityTs, summary.lastActivityTs);
        if (session.statusText.empty()) {
            session.statusText = summary.isLive ? L"Active" : L"Idle";
        }
    }
}

std::vector<AgentSessionSummary> CodexSessionStore::GetSummaries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AgentSessionSummary> summaries;
    summaries.reserve(m_sessions.size());

    for (const auto& pair : m_sessions) {
        const CodexRuntimeSession& session = pair.second;
        AgentSessionSummary summary;
        summary.kind = AgentKind::Codex;
        summary.sessionId = session.sessionId;
        summary.projectPath = session.projectPath;
        summary.title = session.title;
        summary.lastActivityTs = session.lastActivityTs;
        summary.phase = session.phase;
        summary.isLive = session.phase == AgentSessionPhase::Processing || session.phase == AgentSessionPhase::Compacting;
        summary.statusText = session.statusText;
        summary.recentActivityText = session.recentActivityText;
        summary.providerSupportsHooks = true;
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

void CodexSessionStore::UpdateSessionFromEvent(CodexRuntimeSession& session, const CodexHookEvent& event) {
    if (session.projectPath.empty()) {
        session.projectPath = event.cwd;
    }
    session.lastActivityTs = MaxTs(session.lastActivityTs, event.receivedAtMs);
    session.phase = PhaseFromEvent(event);
    session.statusText = DefaultStatusText(session.phase);

    if (event.event == L"PreToolUse" || event.event == L"PostToolUse") {
        session.activeToolUseId = event.toolUseId;
        if (!event.tool.empty() && !event.toolInputSummary.empty()) {
            session.recentActivityText = event.tool + L"  " + MakeExcerpt(event.toolInputSummary);
        } else if (!event.toolInputSummary.empty()) {
            session.recentActivityText = MakeExcerpt(event.toolInputSummary);
        } else if (!event.tool.empty()) {
            session.recentActivityText = event.tool;
        }
    } else if (!event.message.empty()) {
        session.recentActivityText = MakeExcerpt(event.message);
    } else if (!event.event.empty()) {
        session.recentActivityText = event.event;
    }

    if (event.event == L"PostToolUse" && !event.toolUseId.empty() && session.activeToolUseId == event.toolUseId) {
        session.activeToolUseId.clear();
    }

    if (event.event == L"UserPromptSubmit" && !event.message.empty()) {
        session.title = MakeExcerpt(event.message);
    } else if (session.title.empty() && !event.cwd.empty()) {
        session.title = std::filesystem::path(event.cwd).filename().wstring();
    }
}

AgentSessionPhase CodexSessionStore::PhaseFromEvent(const CodexHookEvent& event) {
    if (event.event == L"UserPromptSubmit" || event.event == L"PreToolUse" || event.event == L"PostToolUse") {
        return AgentSessionPhase::Processing;
    }
    if (event.event == L"Stop") {
        return AgentSessionPhase::WaitingForInput;
    }
    if (event.event == L"SessionStart") {
        return AgentSessionPhase::Idle;
    }
    return AgentSessionPhase::Idle;
}

std::wstring CodexSessionStore::DefaultStatusText(AgentSessionPhase phase) {
    switch (phase) {
    case AgentSessionPhase::Processing:
        return L"Processing";
    case AgentSessionPhase::WaitingForInput:
        return L"Waiting for input";
    case AgentSessionPhase::Compacting:
        return L"Compacting";
    case AgentSessionPhase::Ended:
        return L"Ended";
    case AgentSessionPhase::WaitingForApproval:
        return L"Waiting for approval";
    case AgentSessionPhase::Idle:
    default:
        return L"Idle";
    }
}
