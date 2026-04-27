#include "CpInstaller.h"

#include "face_core/CredentialPasswordStore.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <aclapi.h>
#include <filesystem>
#include <string>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace {

constexpr const wchar_t* kCpRegKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\"
    L"{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}";

constexpr const wchar_t* kDllName = L"FaceUnlockProvider.dll";
constexpr const wchar_t* kOrtDll  = L"onnxruntime.dll";

std::wstring CurrentExePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::wstring ProgramDataDir() {
    wchar_t* pd = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) return L"";
    std::wstring r(pd);
    CoTaskMemFree(pd);
    return r + L"\\DynamicIsland";
}

// Grant Authenticated Users (AU) full control of a directory.
// Called by the elevated installer so the regular-user main app can write
// face templates to PROGRAMDATA.
bool GrantAuthUsersFullControl(const std::wstring& dir) {
    PSID pSid = nullptr;
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&auth, 1, SECURITY_AUTHENTICATED_USER_RID,
                                  0,0,0,0,0,0,0, &pSid)) return false;

    EXPLICIT_ACCESSW ea = {};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode        = SET_ACCESS;
    ea.grfInheritance       = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    BuildTrusteeWithSidW(&ea.Trustee, pSid);

    PACL pAcl = nullptr;
    DWORD r = SetEntriesInAclW(1, &ea, nullptr, &pAcl);
    FreeSid(pSid);
    if (r != ERROR_SUCCESS) return false;

    r = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, pAcl, nullptr);
    LocalFree(pAcl);
    return r == ERROR_SUCCESS;
}

bool CopyIfNewer(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool CopyModels(const fs::path& srcDir, const fs::path& dstDir) {
    std::error_code ec;
    fs::create_directories(dstDir, ec);
    for (const auto& entry : fs::directory_iterator(srcDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        CopyIfNewer(entry.path(), dstDir / entry.path().filename());
    }
    return true;
}

bool RunRegsvr32(const std::wstring& dllPath, bool unregister) {
    std::wstring args = unregister ? (L"/s /u \"" + dllPath + L"\"")
                                   : (L"/s \""    + dllPath + L"\"");
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize      = sizeof(sei);
    sei.fMask       = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb      = nullptr;  // already elevated at this point
    sei.lpFile      = L"regsvr32.exe";
    sei.lpParameters= args.c_str();
    sei.nShow       = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return false;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        return code == 0;
    }
    return true;
}

// Trigger our own EXE elevated with a given argument.
// Returns a process handle (caller owns it) or nullptr on failure.
HANDLE SpawnElevated(HWND hwnd, const std::wstring& args) {
    std::wstring exe = CurrentExePath();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize      = sizeof(sei);
    sei.fMask       = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd        = hwnd;
    sei.lpVerb      = L"runas";
    sei.lpFile      = exe.c_str();
    sei.lpParameters= args.c_str();
    sei.nShow       = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return nullptr;
    return sei.hProcess;
}

} // namespace

namespace CpInstaller {

bool IsRegistered() {
    HKEY key = nullptr;
    LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kCpRegKey, 0, KEY_READ, &key);
    if (r == ERROR_SUCCESS) RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool Install(HWND hwndParent, const std::wstring& sourceDir) {
    std::wstring args = L"--install-cp \"" + sourceDir + L"\"";
    HANDLE hProcess = SpawnElevated(hwndParent, args);
    if (!hProcess) return false;
    WaitForSingleObject(hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(hProcess, &code);
    CloseHandle(hProcess);
    return code == 0;
}

bool Uninstall(HWND hwndParent) {
    HANDLE hProcess = SpawnElevated(hwndParent, L"--uninstall-cp");
    if (!hProcess) return false;
    WaitForSingleObject(hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(hProcess, &code);
    CloseHandle(hProcess);
    return code == 0;
}

int RunInstallElevated(const std::wstring& sourceDir) {
    std::wstring destDir = ProgramDataDir();
    if (destDir.empty()) return 1;

    // Create destination directory, migrate the temporary DPAPI password into
    // an LSA secret, then grant write access for template/model updates.
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (!face_core::CredentialPasswordStore::MigrateToLsaSecret()) return 5;
    GrantAuthUsersFullControl(destDir);

    fs::path src(sourceDir);
    fs::path dst(destDir);

    // Copy the CP DLL and ORT runtime.
    if (!CopyIfNewer(src / kDllName, dst / kDllName)) return 2;
    if (!CopyIfNewer(src / kOrtDll,  dst / kOrtDll))  return 3;

    // Copy models directory if present.
    fs::path srcModels = src / L"models";
    if (fs::exists(srcModels, ec)) {
        CopyModels(srcModels, dst / L"models");
    }

    // Register the DLL.
    if (!RunRegsvr32((dst / kDllName).wstring(), false)) return 4;
    return 0;
}

int RunUninstallElevated() {
    std::wstring destDir = ProgramDataDir();
    if (destDir.empty()) return 1;

    fs::path dllPath = fs::path(destDir) / kDllName;
    std::error_code ec;
    if (fs::exists(dllPath, ec)) {
        RunRegsvr32(dllPath.wstring(), true);
        fs::remove(dllPath, ec);
    }
    face_core::CredentialPasswordStore::Clear();
    // Leave onnxruntime.dll and models in place for future re-enable.
    return 0;
}

} // namespace CpInstaller
