#include "ClaudeHookBridge.h"

#include <algorithm>
#include <string>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

using namespace winrt;
using namespace Windows::Data::Json;

namespace {
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\DynamicIslandClaudeHooks";

uint64_t CurrentUnixMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), len, nullptr, nullptr);
    return utf8;
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
}

std::atomic<bool> ClaudeHookBridge::s_listenerRunning{ false };

ClaudeHookBridge::ClaudeHookBridge() = default;

ClaudeHookBridge::~ClaudeHookBridge() {
    Stop();
}

bool ClaudeHookBridge::Start(HookEventHandler handler) {
    Stop();
    m_handler = std::move(handler);
    m_running = true;
    m_worker = std::thread(&ClaudeHookBridge::WorkerLoop, this);
    return true;
}

void ClaudeHookBridge::Stop() {
    m_running = false;

    HANDLE wakePipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wakePipe != INVALID_HANDLE_VALUE) {
        CloseHandle(wakePipe);
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    for (auto& pair : m_pendingPermissions) {
        ClosePipeHandle(pair.second.pipe);
    }
    m_pendingPermissions.clear();
}

bool ClaudeHookBridge::RespondToPermission(const std::wstring& toolUseId, const std::wstring& decision, const std::wstring& reason) {
    PendingPermission pending;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        const auto it = m_pendingPermissions.find(toolUseId);
        if (it == m_pendingPermissions.end()) {
            return false;
        }
        pending = it->second;
        m_pendingPermissions.erase(it);
    }

    JsonObject response;
    response.SetNamedValue(L"decision", JsonValue::CreateStringValue(decision));
    if (!reason.empty()) {
        response.SetNamedValue(L"reason", JsonValue::CreateStringValue(reason));
    }

    std::string payload = WideToUtf8(response.Stringify().c_str());
    payload.push_back('\n');

    DWORD written = 0;
    const BOOL ok = WriteFile(pending.pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    FlushFileBuffers(pending.pipe);
    ClosePipeHandle(pending.pipe);
    return ok == TRUE && written == payload.size();
}

bool ClaudeHookBridge::IsListenerRunning() {
    return s_listenerRunning.load();
}

const wchar_t* ClaudeHookBridge::PipeName() {
    return kPipeName;
}

void ClaudeHookBridge::WorkerLoop() {
    s_listenerRunning = true;
    while (m_running.load()) {
        HANDLE pipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            8192,
            8192,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            break;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            ClosePipeHandle(pipe);
            continue;
        }

        if (!m_running.load()) {
            ClosePipeHandle(pipe);
            break;
        }

        HandleClient(pipe);
    }
    s_listenerRunning = false;
}

void ClaudeHookBridge::HandleClient(HANDLE pipeHandle) {
    std::string message;
    if (!ReadPipeMessage(pipeHandle, message) || message.empty()) {
        ClosePipeHandle(pipeHandle);
        return;
    }

    try {
        JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(message)));
        ClaudeHookEvent event;
        event.sessionId = JsonString(root, L"session_id");
        event.cwd = JsonString(root, L"cwd");
        event.event = JsonString(root, L"event");
        event.status = JsonString(root, L"status");
        event.tool = JsonString(root, L"tool");
        event.toolUseId = JsonString(root, L"tool_use_id");
        event.notificationType = JsonString(root, L"notification_type");
        event.message = JsonString(root, L"message");
        event.receivedAtMs = CurrentUnixMs();

        if (root.HasKey(L"pid")) {
            try {
                event.pid = static_cast<int>(root.GetNamedNumber(L"pid"));
            } catch (...) {
                event.pid = 0;
            }
        }

        JsonObject toolInput = JsonObjectOrEmpty(root, L"tool_input");
        if (toolInput.Size() > 0) {
            event.toolInputSummary = toolInput.Stringify().c_str();
        }

        if (event.event == L"PreToolUse" && !event.toolUseId.empty()) {
            CacheToolUseId(event);
        }

        if (event.event == L"PermissionRequest" && event.toolUseId.empty()) {
            const std::wstring key = BuildCacheKey(event);
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_toolUseIdCache.find(key);
            if (it != m_toolUseIdCache.end() && !it->second.empty()) {
                event.toolUseId = it->second.front();
                it->second.erase(it->second.begin());
                if (it->second.empty()) {
                    m_toolUseIdCache.erase(it);
                }
            }
        }

        if (event.event == L"SessionEnd") {
            PruneToolCache(event.sessionId);
        }

        if (event.event == L"PermissionRequest" && !event.toolUseId.empty()) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingPermissions[event.toolUseId] = { pipeHandle, event.sessionId, event.toolUseId };
            if (m_handler) {
                m_handler(event);
            }
            return;
        }

        if (event.event == L"PostToolUse" && !event.toolUseId.empty()) {
            CompletePendingPermission(event.toolUseId);
        }

        if (m_handler) {
            m_handler(event);
        }
    } catch (...) {
    }

    ClosePipeHandle(pipeHandle);
}

bool ClaudeHookBridge::ReadPipeMessage(HANDLE pipeHandle, std::string& message) const {
    constexpr DWORD kBufferSize = 512;
    char buffer[kBufferSize];
    DWORD bytesRead = 0;

    while (ReadFile(pipeHandle, buffer, kBufferSize, &bytesRead, nullptr) && bytesRead > 0) {
        message.append(buffer, buffer + bytesRead);
        if (!message.empty() && message.back() == '\n') {
            break;
        }
        if (bytesRead < kBufferSize) {
            break;
        }
    }

    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '\0')) {
        message.pop_back();
    }
    return !message.empty();
}

void ClaudeHookBridge::ClosePipeHandle(HANDLE pipeHandle) const {
    if (pipeHandle == INVALID_HANDLE_VALUE) {
        return;
    }
    FlushFileBuffers(pipeHandle);
    DisconnectNamedPipe(pipeHandle);
    CloseHandle(pipeHandle);
}

std::wstring ClaudeHookBridge::BuildCacheKey(const ClaudeHookEvent& event) const {
    return event.sessionId + L"|" + event.tool + L"|" + event.toolInputSummary;
}

void ClaudeHookBridge::CacheToolUseId(const ClaudeHookEvent& event) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_toolUseIdCache[BuildCacheKey(event)].push_back(event.toolUseId);
}

void ClaudeHookBridge::PruneToolCache(const std::wstring& sessionId) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (auto it = m_toolUseIdCache.begin(); it != m_toolUseIdCache.end();) {
        if (it->first.rfind(sessionId + L"|", 0) == 0) {
            it = m_toolUseIdCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ClaudeHookBridge::CompletePendingPermission(const std::wstring& toolUseId) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    auto it = m_pendingPermissions.find(toolUseId);
    if (it == m_pendingPermissions.end()) {
        return;
    }
    ClosePipeHandle(it->second.pipe);
    m_pendingPermissions.erase(it);
}
