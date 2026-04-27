#include "IpcNotifier.h"
#include <windows.h>

namespace FaceCP {

namespace {

constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\DynamicIsland.FaceUnlock";

void Send(const char* msg, DWORD len) {
    HANDLE h = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, msg, len, &written, nullptr);
    CloseHandle(h);
}

} // namespace

void IpcNotifier::SendScanStarted() {
    const char msg[] = R"({"event":"scan_started"})";
    Send(msg, static_cast<DWORD>(sizeof(msg) - 1));
}

void IpcNotifier::SendSuccess(const std::string& userName) {
    std::string j = R"({"event":"success","user":")" + userName + R"("})";
    Send(j.c_str(), static_cast<DWORD>(j.size()));
}

void IpcNotifier::SendFailed(const std::string& reason) {
    std::string j = R"({"event":"failed","reason":")" + reason + R"("})";
    Send(j.c_str(), static_cast<DWORD>(j.size()));
}

} // namespace FaceCP
