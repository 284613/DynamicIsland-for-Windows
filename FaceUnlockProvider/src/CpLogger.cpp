#include "CpLogger.h"

#include <shlobj.h>

#include <cstdarg>
#include <cwchar>
#include <string>

namespace FaceCP {

namespace {

std::wstring LogPath() {
    wchar_t* programData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData))) {
        return L"";
    }

    std::wstring dir(programData);
    CoTaskMemFree(programData);
    dir += L"\\DynamicIsland";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\face_cp.log";
}

std::wstring DebugFlagPath() {
    wchar_t* programData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData))) {
        return L"";
    }

    std::wstring dir(programData);
    CoTaskMemFree(programData);
    dir += L"\\DynamicIsland";
    return dir + L"\\face_cp_debug.flag";
}

bool LoggingEnabled() {
    static int enabled = -1;
    if (enabled >= 0) {
        return enabled != 0;
    }

    const std::wstring flagPath = DebugFlagPath();
    enabled = (!flagPath.empty() && GetFileAttributesW(flagPath.c_str()) != INVALID_FILE_ATTRIBUTES)
              ? 1 : 0;
    return enabled != 0;
}

} // namespace

void CpLog(PCWSTR format, ...) {
    if (!format || !LoggingEnabled()) {
        return;
    }

    wchar_t message[1024] = {};
    va_list args;
    va_start(args, format);
    vswprintf_s(message, format, args);
    va_end(args);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[1400] = {};
    swprintf_s(line,
               L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(),
               message);

    OutputDebugStringW(line);

    const std::wstring path = LogPath();
    if (path.empty()) {
        return;
    }

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

} // namespace FaceCP
