#include "AgentSessionMonitor.h"

#include "Messages.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <shlobj.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Data::Json;

namespace {
constexpr uint64_t kLiveWindowMs = 15000;
constexpr auto kActivePollInterval = std::chrono::seconds(1);
constexpr auto kIdlePollInterval = std::chrono::seconds(5);

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), length);
    return result;
}

uint64_t CurrentUnixMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}

uint64_t NormalizeTimestamp(double value) {
    if (value <= 0.0) {
        return 0;
    }

    if (value >= 1000000000000.0) {
        return static_cast<uint64_t>(value);
    }

    if (value >= 1000000000.0) {
        return static_cast<uint64_t>(value * 1000.0);
    }

    return static_cast<uint64_t>(value);
}

uint64_t ParseIsoTimestampMs(const std::wstring& iso) {
    if (iso.empty()) {
        return 0;
    }

    SYSTEMTIME st{};
    int milliseconds = 0;
    if (swscanf_s(iso.c_str(), L"%4hu-%2hu-%2huT%2hu:%2hu:%2hu.%3dZ",
        &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond, &milliseconds) != 7) {
        milliseconds = 0;
        if (swscanf_s(iso.c_str(), L"%4hu-%2hu-%2huT%2hu:%2hu:%2huZ",
            &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) != 6) {
            return 0;
        }
    }

    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) {
        return 0;
    }

    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000ULL + static_cast<uint64_t>(milliseconds);
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

std::wstring SessionKey(AgentKind kind, const std::wstring& sessionId) {
    return (kind == AgentKind::Claude ? L"claude:" : L"codex:") + sessionId;
}

std::filesystem::path ClaudeHudSnapshotPath(const std::filesystem::path& home) {
    return home / L".claude" / L"dynamic-island-statusline.json";
}

std::wstring JsonString(const JsonObject& object, const wchar_t* key) {
    if (!object.HasKey(key)) {
        return {};
    }

    try {
        return object.GetNamedString(key).c_str();
    } catch (...) {
        return {};
    }
}

double JsonNumber(const JsonObject& object, const wchar_t* key) {
    if (!object.HasKey(key)) {
        return 0.0;
    }

    try {
        return object.GetNamedValue(key).GetNumber();
    } catch (...) {
        return 0.0;
    }
}

JsonObject JsonObjectOrEmpty(const JsonObject& object, const wchar_t* key) {
    if (!object.HasKey(key)) {
        return JsonObject();
    }
    try {
        return object.GetNamedObject(key);
    } catch (...) {
        return JsonObject();
    }
}

std::wstring ExtractContentText(const JsonArray& content) {
    std::wstring text;
    for (const auto& value : content) {
        if (value.ValueType() != JsonValueType::Object) {
            continue;
        }

        JsonObject part = value.GetObject();
        const std::wstring piece = JsonString(part, L"text");
        if (piece.empty()) {
            continue;
        }

        if (!text.empty()) {
            text += L"\n\n";
        }
        text += piece;
    }
    return text;
}

int JsonPercent(const JsonObject& object, const wchar_t* key) {
    const double value = JsonNumber(object, key);
    if (value < 0.0) {
        return -1;
    }
    return static_cast<int>(std::round((std::max)(0.0, (std::min)(100.0, value))));
}

uint64_t JsonResetAtMs(const JsonObject& object, const wchar_t* key) {
    const double value = JsonNumber(object, key);
    if (value <= 0.0) {
        return 0;
    }
    return static_cast<uint64_t>(value * 1000.0);
}

AgentEntryDirection RoleToDirection(const std::wstring& role) {
    if (role == L"user") {
        return AgentEntryDirection::User;
    }
    if (role == L"assistant") {
        return AgentEntryDirection::Assistant;
    }
    return AgentEntryDirection::System;
}

std::filesystem::path GetUserProfilePath() {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &rawPath))) {
        return {};
    }

    std::filesystem::path path(rawPath);
    CoTaskMemFree(rawPath);
    return path;
}

AgentSessionMonitor::FileStamp BuildStamp(const std::filesystem::path& path) {
    AgentSessionMonitor::FileStamp stamp{};
    std::error_code ec;
    stamp.size = std::filesystem::is_regular_file(path, ec) ? std::filesystem::file_size(path, ec) : 0;
    ec.clear();
    const auto lastWrite = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        stamp.writeTicks = static_cast<long long>(lastWrite.time_since_epoch().count());
    }
    return stamp;
}

void EnsureRecordProject(AgentSessionMonitor::SessionRecord& record, const std::wstring& projectPath) {
    if (record.projectPath.empty() && !projectPath.empty()) {
        record.projectPath = projectPath;
    }
}

void UpdateRecordTitle(AgentSessionMonitor::SessionRecord& record, AgentEntryDirection direction, const std::wstring& text) {
    if (text.empty()) {
        return;
    }

    if (direction == AgentEntryDirection::User || record.title.empty()) {
        record.title = MakeExcerpt(text);
    }
}

void AddHistoryEntry(AgentSessionMonitor::SessionRecord& record,
    AgentEntryDirection direction,
    uint64_t timestamp,
    const std::wstring& text) {
    const std::wstring collapsed = CollapseWhitespace(text);
    if (collapsed.empty()) {
        return;
    }

    if (!record.history.empty()) {
        const AgentHistoryEntry& last = record.history.back();
        if (last.direction == direction &&
            last.text == collapsed &&
            ((last.timestamp <= timestamp && timestamp - last.timestamp <= 1000) ||
             (timestamp <= last.timestamp && last.timestamp - timestamp <= 1000))) {
            record.lastActivityTs = (std::max)(record.lastActivityTs, timestamp);
            UpdateRecordTitle(record, direction, collapsed);
            if (direction == AgentEntryDirection::Assistant) {
                record.hasRichHistory = true;
            }
            return;
        }
    }

    record.history.push_back({ record.kind, record.sessionId, timestamp, direction, collapsed });
    record.lastActivityTs = (std::max)(record.lastActivityTs, timestamp);
    UpdateRecordTitle(record, direction, collapsed);
    if (direction == AgentEntryDirection::Assistant) {
        record.hasRichHistory = true;
    }
}

void SortRecordHistory(AgentSessionMonitor::SessionRecord& record) {
    std::sort(record.history.begin(), record.history.end(), [](const AgentHistoryEntry& lhs, const AgentHistoryEntry& rhs) {
        if (lhs.timestamp != rhs.timestamp) {
            return lhs.timestamp < rhs.timestamp;
        }
        return static_cast<int>(lhs.direction) < static_cast<int>(rhs.direction);
    });

    record.history.erase(std::unique(record.history.begin(), record.history.end(), [](const AgentHistoryEntry& lhs, const AgentHistoryEntry& rhs) {
        return lhs.timestamp == rhs.timestamp &&
            lhs.direction == rhs.direction &&
            lhs.text == rhs.text;
    }), record.history.end());
}

void LoadClaudeSessionMeta(std::unordered_map<std::wstring, AgentSessionMonitor::SessionRecord>& records,
    const std::filesystem::path& sessionsDir) {
    if (!std::filesystem::exists(sessionsDir)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sessionsDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".json") {
            continue;
        }

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (content.empty()) {
            continue;
        }

        try {
            const JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(content)));
            const std::wstring sessionId = JsonString(root, L"sessionId");
            if (sessionId.empty()) {
                continue;
            }

            auto& record = records[SessionKey(AgentKind::Claude, sessionId)];
            record.kind = AgentKind::Claude;
            record.sessionId = sessionId;
            EnsureRecordProject(record, JsonString(root, L"cwd"));
            record.lastActivityTs = (std::max)(record.lastActivityTs, NormalizeTimestamp(JsonNumber(root, L"startedAt")));
        } catch (...) {
        }
    }
}

void LoadClaudeHistory(std::unordered_map<std::wstring, AgentSessionMonitor::SessionRecord>& records,
    const std::filesystem::path& historyPath) {
    std::ifstream file(historyPath, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            const JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(line)));
            const std::wstring sessionId = JsonString(root, L"sessionId");
            const std::wstring prompt = JsonString(root, L"display");
            if (sessionId.empty() || prompt.empty()) {
                continue;
            }

            auto& record = records[SessionKey(AgentKind::Claude, sessionId)];
            record.kind = AgentKind::Claude;
            record.sessionId = sessionId;
            EnsureRecordProject(record, JsonString(root, L"project"));
            AddHistoryEntry(record, AgentEntryDirection::User, NormalizeTimestamp(JsonNumber(root, L"timestamp")), prompt);
        } catch (...) {
        }
    }
}

void LoadCodexSessions(std::unordered_map<std::wstring, AgentSessionMonitor::SessionRecord>& records,
    const std::filesystem::path& sessionsDir) {
    if (!std::filesystem::exists(sessionsDir)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sessionsDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".jsonl") {
            continue;
        }

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) {
            continue;
        }

        std::wstring currentSessionId;
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            try {
                const JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(line)));
                const std::wstring type = JsonString(root, L"type");
                const uint64_t timestamp = ParseIsoTimestampMs(JsonString(root, L"timestamp"));

                if (type == L"session_meta") {
                    JsonObject payload = root.GetNamedObject(L"payload");
                    const std::wstring sessionId = JsonString(payload, L"id");
                    if (sessionId.empty()) {
                        continue;
                    }

                    currentSessionId = sessionId;
                    auto& record = records[SessionKey(AgentKind::Codex, sessionId)];
                    record.kind = AgentKind::Codex;
                    record.sessionId = sessionId;
                    EnsureRecordProject(record, JsonString(payload, L"cwd"));
                    record.lastActivityTs = (std::max)(record.lastActivityTs, timestamp);
                    continue;
                }

                if (type == L"event_msg") {
                    JsonObject payload = root.GetNamedObject(L"payload");
                    const std::wstring payloadType = JsonString(payload, L"type");
                    const std::wstring sessionId = currentSessionId;
                    const std::wstring text = JsonString(payload, L"message");
                    auto& record = records[SessionKey(AgentKind::Codex, sessionId)];
                    record.kind = AgentKind::Codex;
                    record.sessionId = sessionId;

                    if (payloadType == L"token_count") {
                        JsonObject info = JsonObjectOrEmpty(payload, L"info");
                        JsonObject lastUsage = JsonObjectOrEmpty(info, L"last_token_usage");
                        JsonObject totalUsage = JsonObjectOrEmpty(info, L"total_token_usage");
                        const double modelContextWindow = JsonNumber(info, L"model_context_window");
                        const double lastInputTokens = JsonNumber(lastUsage, L"input_tokens");
                        record.hasHudData = true;
                        record.modelName = L"Codex";
                        record.inputTokens = static_cast<uint64_t>((std::max)(0.0, JsonNumber(totalUsage, L"input_tokens")));
                        record.outputTokens = static_cast<uint64_t>((std::max)(0.0, JsonNumber(totalUsage, L"output_tokens")));
                        if (modelContextWindow > 0.0 && lastInputTokens >= 0.0) {
                            const int contextUsedPercent = static_cast<int>(std::round((std::max)(0.0, (std::min)(100.0, (lastInputTokens / modelContextWindow) * 100.0))));
                            record.contextUsedPercent = contextUsedPercent;
                            record.contextRemainingPercent = (std::max)(0, 100 - contextUsedPercent);
                        }

                        JsonObject rateLimits = JsonObjectOrEmpty(payload, L"rate_limits");
                        JsonObject primary = JsonObjectOrEmpty(rateLimits, L"primary");
                        JsonObject secondary = JsonObjectOrEmpty(rateLimits, L"secondary");
                        record.fiveHourUsedPercent = JsonPercent(primary, L"used_percent");
                        record.sevenDayUsedPercent = JsonPercent(secondary, L"used_percent");
                        record.fiveHourResetAtMs = JsonResetAtMs(primary, L"resets_at");
                        record.sevenDayResetAtMs = JsonResetAtMs(secondary, L"resets_at");
                        continue;
                    }

                    if (sessionId.empty() || text.empty()) {
                        continue;
                    }

                    if (payloadType == L"user_message") {
                        AddHistoryEntry(record, AgentEntryDirection::User, timestamp, text);
                    } else if (payloadType == L"agent_message") {
                        AddHistoryEntry(record, AgentEntryDirection::Assistant, timestamp, text);
                    }
                    continue;
                }

                if (type == L"user_message") {
                    JsonObject payload = root.GetNamedObject(L"payload");
                    const std::wstring sessionId = JsonString(payload, L"session_id").empty()
                        ? currentSessionId
                        : JsonString(payload, L"session_id");
                    const std::wstring text = JsonString(payload, L"text");
                    if (sessionId.empty() || text.empty()) {
                        continue;
                    }

                    auto& record = records[SessionKey(AgentKind::Codex, sessionId)];
                    record.kind = AgentKind::Codex;
                    record.sessionId = sessionId;
                    AddHistoryEntry(record, AgentEntryDirection::User, timestamp, text);
                    continue;
                }

                if (type != L"response_item") {
                    continue;
                }

                JsonObject payload = root.GetNamedObject(L"payload");
                const std::wstring payloadType = JsonString(payload, L"type");
                const std::wstring payloadSessionId = JsonString(payload, L"session_id");
                const std::wstring sessionId = payloadSessionId.empty() ? currentSessionId : payloadSessionId;
                auto& record = records[SessionKey(AgentKind::Codex, sessionId)];
                record.kind = AgentKind::Codex;
                record.sessionId = sessionId;
                if (sessionId.empty()) {
                    continue;
                }

                if (payloadType == L"message") {
                    const std::wstring role = JsonString(payload, L"role");
                    if (role != L"user" && role != L"assistant" && role != L"system") {
                        continue;
                    }

                    std::wstring text;
                    if (payload.HasKey(L"content")) {
                        text = ExtractContentText(payload.GetNamedArray(L"content"));
                    }
                    if (text.empty()) {
                        continue;
                    }
                    AddHistoryEntry(record, RoleToDirection(role), timestamp, text);
                    continue;
                }

                if (payloadType == L"function_call") {
                    const std::wstring name = JsonString(payload, L"name");
                    const std::wstring arguments = JsonString(payload, L"arguments");
                    std::wstring text = name.empty() ? L"Tool call" : name;
                    if (!arguments.empty()) {
                        text += L"  " + MakeExcerpt(arguments);
                    }
                    AddHistoryEntry(record, AgentEntryDirection::System, timestamp, text);
                    continue;
                }

                if (payloadType == L"function_call_output") {
                    const std::wstring output = JsonString(payload, L"output");
                    if (!output.empty()) {
                        AddHistoryEntry(record, AgentEntryDirection::System, timestamp, MakeExcerpt(output));
                    }
                    continue;
                }
            } catch (...) {
            }
        }
    }
}

void LoadCodexHistoryIndex(std::unordered_map<std::wstring, AgentSessionMonitor::SessionRecord>& records,
    const std::filesystem::path& historyPath) {
    std::ifstream file(historyPath, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            const JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(line)));
            const std::wstring sessionId = JsonString(root, L"session_id");
            const std::wstring text = JsonString(root, L"text");
            if (sessionId.empty() || text.empty()) {
                continue;
            }

            auto& record = records[SessionKey(AgentKind::Codex, sessionId)];
            record.kind = AgentKind::Codex;
            record.sessionId = sessionId;
            const uint64_t timestamp = NormalizeTimestamp(JsonNumber(root, L"ts"));
            const bool isNewest = timestamp >= record.lastActivityTs;
            record.lastActivityTs = (std::max)(record.lastActivityTs, timestamp);
            if (record.title.empty() || isNewest) {
                record.title = MakeExcerpt(text);
            }
        } catch (...) {
        }
    }
}

AgentSessionMonitor::ClaudeHudSnapshot LoadClaudeHudSnapshot(const std::filesystem::path& snapshotPath) {
    AgentSessionMonitor::ClaudeHudSnapshot snapshot;
    std::ifstream file(snapshotPath, std::ios::binary);
    if (!file.is_open()) {
        return snapshot;
    }

    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return snapshot;
    }

    try {
        const JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(bytes)));
        snapshot.cwd = JsonString(root, L"cwd");

        JsonObject modelObject = JsonObjectOrEmpty(root, L"model");
        snapshot.modelName = JsonString(modelObject, L"display_name");
        if (snapshot.modelName.empty()) {
            snapshot.modelName = JsonString(modelObject, L"id");
        }

        JsonObject contextWindow = JsonObjectOrEmpty(root, L"context_window");
        snapshot.contextUsedPercent = JsonPercent(contextWindow, L"used_percentage");
        snapshot.contextRemainingPercent = JsonPercent(contextWindow, L"remaining_percentage");

        JsonObject currentUsage = JsonObjectOrEmpty(contextWindow, L"current_usage");
        snapshot.inputTokens = static_cast<uint64_t>((std::max)(0.0, JsonNumber(currentUsage, L"input_tokens")));
        snapshot.outputTokens = static_cast<uint64_t>((std::max)(0.0, JsonNumber(currentUsage, L"output_tokens")));
        snapshot.cacheCreationTokens = static_cast<uint64_t>((std::max)(0.0, JsonNumber(currentUsage, L"cache_creation_input_tokens")));
        snapshot.cacheReadTokens = static_cast<uint64_t>((std::max)(0.0, JsonNumber(currentUsage, L"cache_read_input_tokens")));

        JsonObject rateLimits = JsonObjectOrEmpty(root, L"rate_limits");
        JsonObject fiveHour = JsonObjectOrEmpty(rateLimits, L"five_hour");
        JsonObject sevenDay = JsonObjectOrEmpty(rateLimits, L"seven_day");
        snapshot.fiveHourUsedPercent = JsonPercent(fiveHour, L"used_percentage");
        snapshot.sevenDayUsedPercent = JsonPercent(sevenDay, L"used_percentage");
        snapshot.fiveHourResetAtMs = JsonResetAtMs(fiveHour, L"resets_at");
        snapshot.sevenDayResetAtMs = JsonResetAtMs(sevenDay, L"resets_at");
    } catch (...) {
    }

    return snapshot;
}
}

AgentSessionMonitor::AgentSessionMonitor() = default;

AgentSessionMonitor::~AgentSessionMonitor() {
    Shutdown();
}

bool AgentSessionMonitor::Initialize(HWND targetHwnd) {
    Shutdown();
    m_targetHwnd = targetHwnd;
    m_running = true;
    m_worker = std::thread(&AgentSessionMonitor::WorkerLoop, this);
    return true;
}

void AgentSessionMonitor::RequestRefresh() {
    {
        std::lock_guard<std::mutex> lock(m_waitMutex);
        m_refreshRequested = true;
    }
    m_waitCv.notify_all();
}

void AgentSessionMonitor::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_waitMutex);
        m_running = false;
        m_refreshRequested = false;
    }
    m_waitCv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

std::vector<AgentSessionSummary> AgentSessionMonitor::GetSummaries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_summaries;
}

std::vector<AgentHistoryEntry> AgentSessionMonitor::GetHistory(AgentKind kind, const std::wstring& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_records.find(SessionKey(kind, sessionId));
    if (it == m_records.end()) {
        return {};
    }
    return it->second.history;
}

void AgentSessionMonitor::WorkerLoop() {
    while (true) {
        bool running = false;
        {
            std::lock_guard<std::mutex> lock(m_waitMutex);
            running = m_running;
        }
        if (!running) {
            break;
        }

        const bool sourceChanged = RefreshSources();
        const bool liveChanged = RefreshLiveStateOnly();
        if ((sourceChanged || liveChanged) && m_targetHwnd) {
            PostMessageW(m_targetHwnd, WM_AGENT_SESSIONS_UPDATED, 0, 0);
        }

        std::chrono::milliseconds waitInterval = kIdlePollInterval;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const bool hasLiveSummary = std::any_of(m_summaries.begin(), m_summaries.end(), [](const AgentSessionSummary& summary) {
                return summary.isLive;
            });
            waitInterval = hasLiveSummary ? kActivePollInterval : kIdlePollInterval;
        }

        std::unique_lock<std::mutex> waitLock(m_waitMutex);
        m_waitCv.wait_for(waitLock, waitInterval, [this]() { return !m_running || m_refreshRequested; });
        m_refreshRequested = false;
        if (!m_running) {
            break;
        }
    }
}

bool AgentSessionMonitor::RefreshSources() {
    const std::filesystem::path home = GetUserProfilePath();
    if (home.empty()) {
        return false;
    }

    const std::filesystem::path claudeHistory = home / L".claude" / L"history.jsonl";
    const std::filesystem::path claudeSessions = home / L".claude" / L"sessions";
    const std::filesystem::path claudeHudSnapshot = ClaudeHudSnapshotPath(home);
    const std::filesystem::path codexHistory = home / L".codex" / L"history.jsonl";
    const std::filesystem::path codexSessions = home / L".codex" / L"sessions";

    std::unordered_map<std::wstring, FileStamp> latestStamps;
    auto collectPath = [&latestStamps](const std::filesystem::path& path) {
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
            latestStamps[path.wstring()] = BuildStamp(path);
        }
    };

    collectPath(claudeHistory);
    collectPath(claudeHudSnapshot);
    collectPath(codexHistory);

    if (std::filesystem::exists(claudeSessions)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(claudeSessions)) {
            if (entry.is_regular_file() && entry.path().extension() == L".json") {
                latestStamps[entry.path().wstring()] = BuildStamp(entry.path());
            }
        }
    }

    if (std::filesystem::exists(codexSessions)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(codexSessions)) {
            if (entry.is_regular_file() && entry.path().extension() == L".jsonl") {
                latestStamps[entry.path().wstring()] = BuildStamp(entry.path());
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (latestStamps == m_fileStamps) {
            return false;
        }
    }

    std::unordered_map<std::wstring, SessionRecord> rebuiltRecords;
    LoadClaudeSessionMeta(rebuiltRecords, claudeSessions);
    LoadClaudeHistory(rebuiltRecords, claudeHistory);
    LoadCodexSessions(rebuiltRecords, codexSessions);
    LoadCodexHistoryIndex(rebuiltRecords, codexHistory);
    ClaudeHudSnapshot hudSnapshot = LoadClaudeHudSnapshot(claudeHudSnapshot);

    for (auto& pair : rebuiltRecords) {
        SessionRecord& record = pair.second;
        SortRecordHistory(record);
        if (record.title.empty() && !record.history.empty()) {
            record.title = MakeExcerpt(record.history.back().text);
        }
        if (record.lastActivityTs == 0 && !record.history.empty()) {
            record.lastActivityTs = record.history.back().timestamp;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_records = std::move(rebuiltRecords);
        m_fileStamps = std::move(latestStamps);
        m_claudeHudSnapshot = std::move(hudSnapshot);
        RebuildSnapshotsLocked();
    }
    return true;
}

bool AgentSessionMonitor::RefreshLiveStateOnly() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t now = CurrentUnixMs();
    bool changed = false;

    for (auto& summary : m_summaries) {
        const bool shouldBeLive = summary.lastActivityTs > 0 && (now - summary.lastActivityTs) <= kLiveWindowMs;
        if (summary.isLive != shouldBeLive) {
            summary.isLive = shouldBeLive;
            changed = true;
        }
    }

    return changed;
}

void AgentSessionMonitor::RebuildSnapshotsLocked() {
    m_summaries.clear();
    m_summaries.reserve(m_records.size());

    const uint64_t now = CurrentUnixMs();
    for (const auto& pair : m_records) {
        const SessionRecord& record = pair.second;
        if (record.sessionId.empty()) {
            continue;
        }

        AgentSessionSummary summary;
        summary.kind = record.kind;
        summary.sessionId = record.sessionId;
        summary.projectPath = record.projectPath;
        summary.title = record.title;
        summary.lastActivityTs = record.lastActivityTs;
        summary.itemCount = record.history.size();
        summary.hasRichHistory = record.hasRichHistory;
        summary.isLive = record.lastActivityTs > 0 && (now - record.lastActivityTs) <= kLiveWindowMs;
        summary.phase = summary.kind == AgentKind::Claude
            ? (summary.isLive ? AgentSessionPhase::Processing : AgentSessionPhase::Idle)
            : (summary.isLive ? AgentSessionPhase::Processing : AgentSessionPhase::Idle);
        summary.statusText = summary.kind == AgentKind::Claude
            ? (summary.isLive ? L"Working" : L"Idle")
            : (summary.isLive ? L"Active" : L"Idle");
        if (record.hasHudData) {
            summary.hasHudData = true;
            summary.modelName = record.modelName;
            summary.contextUsedPercent = record.contextUsedPercent;
            summary.contextRemainingPercent = record.contextRemainingPercent;
            summary.inputTokens = record.inputTokens;
            summary.outputTokens = record.outputTokens;
            summary.cacheCreationTokens = record.cacheCreationTokens;
            summary.cacheReadTokens = record.cacheReadTokens;
            summary.fiveHourUsedPercent = record.fiveHourUsedPercent;
            summary.sevenDayUsedPercent = record.sevenDayUsedPercent;
            summary.fiveHourResetAtMs = record.fiveHourResetAtMs;
            summary.sevenDayResetAtMs = record.sevenDayResetAtMs;
        }
        m_summaries.push_back(std::move(summary));
    }

    if (!m_claudeHudSnapshot.cwd.empty()) {
        auto match = std::find_if(m_summaries.begin(), m_summaries.end(), [this](const AgentSessionSummary& summary) {
            return summary.kind == AgentKind::Claude &&
                (summary.projectPath == m_claudeHudSnapshot.cwd ||
                 std::filesystem::path(summary.projectPath).filename().wstring() == std::filesystem::path(m_claudeHudSnapshot.cwd).filename().wstring());
        });
        if (match != m_summaries.end()) {
            match->hasHudData = true;
            match->modelName = m_claudeHudSnapshot.modelName;
            match->contextUsedPercent = m_claudeHudSnapshot.contextUsedPercent;
            match->contextRemainingPercent = m_claudeHudSnapshot.contextRemainingPercent;
            match->inputTokens = m_claudeHudSnapshot.inputTokens;
            match->outputTokens = m_claudeHudSnapshot.outputTokens;
            match->cacheCreationTokens = m_claudeHudSnapshot.cacheCreationTokens;
            match->cacheReadTokens = m_claudeHudSnapshot.cacheReadTokens;
            match->fiveHourUsedPercent = m_claudeHudSnapshot.fiveHourUsedPercent;
            match->sevenDayUsedPercent = m_claudeHudSnapshot.sevenDayUsedPercent;
            match->fiveHourResetAtMs = m_claudeHudSnapshot.fiveHourResetAtMs;
            match->sevenDayResetAtMs = m_claudeHudSnapshot.sevenDayResetAtMs;
        }
    }

    std::sort(m_summaries.begin(), m_summaries.end(), [](const AgentSessionSummary& lhs, const AgentSessionSummary& rhs) {
        if (lhs.lastActivityTs != rhs.lastActivityTs) {
            return lhs.lastActivityTs > rhs.lastActivityTs;
        }
        if (lhs.kind != rhs.kind) {
            return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
        }
        return lhs.sessionId < rhs.sessionId;
    });
}
