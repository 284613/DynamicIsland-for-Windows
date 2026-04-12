#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class AgentKind {
    Claude,
    Codex
};

enum class AgentEntryDirection {
    User,
    Assistant,
    System
};

enum class AgentSessionPhase {
    Idle,
    Processing,
    WaitingForApproval,
    WaitingForInput,
    Compacting,
    Ended
};

enum class AgentSessionFilter {
    Claude,
    Codex
};

struct AgentSessionSummary {
    AgentKind kind = AgentKind::Claude;
    std::wstring sessionId;
    std::wstring projectPath;
    std::wstring title;
    uint64_t lastActivityTs = 0;
    size_t itemCount = 0;
    bool hasRichHistory = false;
    bool isLive = false;
    AgentSessionPhase phase = AgentSessionPhase::Idle;
    std::wstring statusText;
    std::wstring pendingToolName;
    std::wstring pendingToolUseId;
    std::wstring recentActivityText;
    bool providerSupportsHooks = false;
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
};

struct AgentHistoryEntry {
    AgentKind kind = AgentKind::Claude;
    std::wstring sessionId;
    uint64_t timestamp = 0;
    AgentEntryDirection direction = AgentEntryDirection::System;
    std::wstring text;
};

std::wstring AgentKindLabel(AgentKind kind);
bool AgentSessionMatchesFilter(const AgentSessionSummary& summary, AgentSessionFilter filter);
std::wstring AgentPhaseLabel(AgentSessionPhase phase);
