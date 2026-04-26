#include "AutoStartManager.h"

#include <windows.h>

namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"DynamicIsland";

std::wstring CurrentExePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return path;
}

} // namespace

namespace AutoStart {

std::wstring CommandLine() {
    std::wstring exe = CurrentExePath();
    if (exe.empty()) {
        return {};
    }
    return L"\"" + exe + L"\" --autostart";
}

bool IsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[1024] = {};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    LONG result = RegQueryValueExW(key, kRunValue, nullptr, &type,
        reinterpret_cast<LPBYTE>(value), &bytes);
    RegCloseKey(key);

    return result == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

bool SetEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enabled) {
        std::wstring command = CommandLine();
        ok = !command.empty() &&
            RegSetValueExW(key, kRunValue, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LONG result = RegDeleteValueW(key, kRunValue);
        ok = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(key);
    return ok;
}

} // namespace AutoStart
