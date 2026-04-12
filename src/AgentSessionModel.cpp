#include "AgentSessionModel.h"

std::wstring AgentKindLabel(AgentKind kind) {
    return kind == AgentKind::Claude ? L"Claude" : L"Codex";
}

bool AgentSessionMatchesFilter(const AgentSessionSummary& summary, AgentSessionFilter filter) {
    switch (filter) {
    case AgentSessionFilter::Claude:
        return summary.kind == AgentKind::Claude;
    case AgentSessionFilter::Codex:
    default:
        return summary.kind == AgentKind::Codex;
    }
}

std::wstring AgentPhaseLabel(AgentSessionPhase phase) {
    switch (phase) {
    case AgentSessionPhase::Processing:
        return L"Processing";
    case AgentSessionPhase::WaitingForApproval:
        return L"WaitingForApproval";
    case AgentSessionPhase::WaitingForInput:
        return L"WaitingForInput";
    case AgentSessionPhase::Compacting:
        return L"Compacting";
    case AgentSessionPhase::Ended:
        return L"Ended";
    case AgentSessionPhase::Idle:
    default:
        return L"Idle";
    }
}
