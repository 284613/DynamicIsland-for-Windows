#include "CodexHookBridge.h"

#include <string>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

using namespace winrt;
using namespace Windows::Data::Json;

namespace {
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\DynamicIslandCodexHooks";

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

    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), length);
    return result;
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
}

std::atomic<bool> CodexHookBridge::s_listenerRunning{ false };

CodexHookBridge::CodexHookBridge() = default;

CodexHookBridge::~CodexHookBridge() {
    Stop();
}

bool CodexHookBridge::Start(HookEventHandler handler) {
    Stop();
    m_handler = std::move(handler);
    m_running = true;
    m_worker = std::thread(&CodexHookBridge::WorkerLoop, this);
    return true;
}

void CodexHookBridge::Stop() {
    m_running = false;

    HANDLE wakePipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wakePipe != INVALID_HANDLE_VALUE) {
        CloseHandle(wakePipe);
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool CodexHookBridge::IsListenerRunning() {
    return s_listenerRunning.load();
}

const wchar_t* CodexHookBridge::PipeName() {
    return kPipeName;
}

void CodexHookBridge::WorkerLoop() {
    s_listenerRunning = true;
    while (m_running.load()) {
        HANDLE pipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,
            4096,
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

void CodexHookBridge::HandleClient(HANDLE pipeHandle) {
    std::string message;
    if (!ReadPipeMessage(pipeHandle, message) || message.empty()) {
        ClosePipeHandle(pipeHandle);
        return;
    }

    try {
        const JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(message)));
        CodexHookEvent event;
        event.sessionId = JsonString(root, L"session_id");
        event.cwd = JsonString(root, L"cwd");
        event.turnId = JsonString(root, L"turn_id");
        event.event = JsonString(root, L"event");
        event.tool = JsonString(root, L"tool");
        event.toolInputSummary = JsonString(root, L"tool_input_summary");
        event.toolUseId = JsonString(root, L"tool_use_id");
        event.permissionMode = JsonString(root, L"permission_mode");
        event.source = JsonString(root, L"source");
        event.message = JsonString(root, L"message");
        event.receivedAtMs = CurrentUnixMs();

        if (m_handler) {
            m_handler(event);
        }
    } catch (...) {
    }

    ClosePipeHandle(pipeHandle);
}

bool CodexHookBridge::ReadPipeMessage(HANDLE pipeHandle, std::string& message) const {
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

    while (!message.empty() &&
        (message.back() == '\n' || message.back() == '\r' || message.back() == '\0')) {
        message.pop_back();
    }
    return !message.empty();
}

void CodexHookBridge::ClosePipeHandle(HANDLE pipeHandle) const {
    if (pipeHandle == INVALID_HANDLE_VALUE) {
        return;
    }
    FlushFileBuffers(pipeHandle);
    DisconnectNamedPipe(pipeHandle);
    CloseHandle(pipeHandle);
}
