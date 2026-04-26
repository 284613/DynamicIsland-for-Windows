#include "FaceUnlockBridge.h"

#include "Messages.h"

#include <sddl.h>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace {

constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\DynamicIsland.FaceUnlock";

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

std::wstring CurrentUserSidString() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }

    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);

    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sidText)) {
        return {};
    }

    std::wstring result = sidText;
    LocalFree(sidText);
    return result;
}

PSECURITY_DESCRIPTOR CreatePipeSecurityDescriptor() {
    std::wstring userSid = CurrentUserSidString();
    std::wstring sddl = L"D:P(A;;GA;;;SY)";
    if (!userSid.empty()) {
        sddl += L"(A;;GA;;;" + userSid + L")";
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return nullptr;
    }
    return descriptor;
}

std::string ExtractJsonString(const std::string& json, const char* key) {
    std::string marker = std::string("\"") + key + "\"";
    size_t keyPos = json.find(marker);
    if (keyPos == std::string::npos) {
        return {};
    }
    size_t colon = json.find(':', keyPos + marker.size());
    if (colon == std::string::npos) {
        return {};
    }
    size_t firstQuote = json.find('"', colon + 1);
    if (firstQuote == std::string::npos) {
        return {};
    }
    size_t secondQuote = json.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) {
        return {};
    }
    return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

} // namespace

FaceUnlockBridge::~FaceUnlockBridge() {
    Stop();
}

bool FaceUnlockBridge::Start(HWND notifyHwnd) {
    if (m_worker.joinable()) {
        return true;
    }
    m_notifyHwnd = notifyHwnd;
    m_stop = false;
    m_worker = std::thread([this]() { WorkerLoop(); });
    return true;
}

void FaceUnlockBridge::Stop() {
    m_stop = true;

    HANDLE client = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (client != INVALID_HANDLE_VALUE) {
        CloseHandle(client);
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void FaceUnlockBridge::WorkerLoop() {
    while (!m_stop) {
        PSECURITY_DESCRIPTOR descriptor = CreatePipeSecurityDescriptor();
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = descriptor;
        sa.bInheritHandle = FALSE;

        HANDLE pipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            descriptor ? &sa : nullptr);

        if (descriptor) {
            LocalFree(descriptor);
        }

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected && !m_stop) {
            char buffer[4096] = {};
            DWORD read = 0;
            if (ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr) && read > 0) {
                buffer[read] = '\0';
                PostEventFromJson(std::string(buffer, read));
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void FaceUnlockBridge::PostEventFromJson(const std::string& json) {
    if (!m_notifyHwnd || !IsWindow(m_notifyHwnd)) {
        return;
    }

    auto* event = new FaceUnlockEvent();
    const std::string type = ExtractJsonString(json, "event");
    if (type == "scan_started") {
        event->kind = FaceUnlockEventKind::ScanStarted;
    } else if (type == "success") {
        event->kind = FaceUnlockEventKind::Success;
        event->user = Utf8ToWide(ExtractJsonString(json, "user"));
    } else if (type == "failed") {
        event->kind = FaceUnlockEventKind::Failed;
        event->reason = Utf8ToWide(ExtractJsonString(json, "reason"));
    } else {
        delete event;
        return;
    }

    PostMessageW(m_notifyHwnd, WM_FACE_UNLOCK_EVENT, 0, reinterpret_cast<LPARAM>(event));
}
