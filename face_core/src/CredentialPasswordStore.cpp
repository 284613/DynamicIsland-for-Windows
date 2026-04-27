#include "face_core/CredentialPasswordStore.h"

#include <windows.h>
#include <dpapi.h>
#include <ntsecapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstring>
#include <filesystem>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Crypt32.lib")

namespace fs = std::filesystem;

namespace face_core {

namespace {

constexpr const wchar_t* kSecretName = L"L$DynamicIsland.FaceUnlock.Password";

void SecureWString(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

LSA_UNICODE_STRING MakeLsaString(const std::wstring& value) {
    LSA_UNICODE_STRING result{};
    result.Length = static_cast<USHORT>(value.size() * sizeof(wchar_t));
    result.MaximumLength = static_cast<USHORT>((value.size() + 1) * sizeof(wchar_t));
    result.Buffer = const_cast<PWSTR>(value.c_str());
    return result;
}

bool OpenPolicy(ACCESS_MASK access, LSA_HANDLE& policy) {
    policy = nullptr;
    LSA_OBJECT_ATTRIBUTES attrs{};
    NTSTATUS status = LsaOpenPolicy(nullptr, &attrs, access, &policy);
    return LsaNtStatusToWinError(status) == ERROR_SUCCESS;
}

bool StoreLsaPassword(const std::wstring& password) {
    if (password.empty()) {
        return false;
    }

    LSA_HANDLE policy = nullptr;
    if (!OpenPolicy(POLICY_CREATE_SECRET, policy)) {
        return false;
    }

    const std::wstring keyName(kSecretName);
    LSA_UNICODE_STRING key = MakeLsaString(keyName);
    LSA_UNICODE_STRING data = MakeLsaString(password);
    NTSTATUS status = LsaStorePrivateData(policy, &key, &data);
    LsaClose(policy);
    return LsaNtStatusToWinError(status) == ERROR_SUCCESS;
}

bool LoadLsaPassword(std::wstring& password) {
    password.clear();

    LSA_HANDLE policy = nullptr;
    if (!OpenPolicy(POLICY_GET_PRIVATE_INFORMATION, policy)) {
        return false;
    }

    const std::wstring keyName(kSecretName);
    LSA_UNICODE_STRING key = MakeLsaString(keyName);
    PLSA_UNICODE_STRING data = nullptr;
    NTSTATUS status = LsaRetrievePrivateData(policy, &key, &data);
    LsaClose(policy);

    if (LsaNtStatusToWinError(status) != ERROR_SUCCESS || !data || !data->Buffer) {
        if (data) {
            LsaFreeMemory(data);
        }
        return false;
    }

    password.assign(data->Buffer, data->Length / sizeof(wchar_t));
    SecureZeroMemory(data->Buffer, data->MaximumLength);
    LsaFreeMemory(data);
    return true;
}

bool DeleteLsaPassword() {
    LSA_HANDLE policy = nullptr;
    if (!OpenPolicy(POLICY_CREATE_SECRET, policy)) {
        return false;
    }

    const std::wstring keyName(kSecretName);
    LSA_UNICODE_STRING key = MakeLsaString(keyName);
    NTSTATUS status = LsaStorePrivateData(policy, &key, nullptr);
    LsaClose(policy);

    DWORD error = LsaNtStatusToWinError(status);
    return error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND || error == ERROR_NOT_FOUND;
}

bool SaveDpapiRecord(const std::wstring& username, const std::wstring& password,
                     const std::wstring& userSid) {
    std::wstring path = CredentialPasswordStore::Path();
    if (path.empty()) return false;

    // Plaintext layout: [u32 len][len wchars] for username, password, then
    // optional SID. len includes the null terminator.
    std::vector<uint8_t> plain;
    auto appendStr = [&](const std::wstring& s) {
        uint32_t len = static_cast<uint32_t>(s.size() + 1);
        const auto* lp = reinterpret_cast<const uint8_t*>(&len);
        plain.insert(plain.end(), lp, lp + 4);
        const auto* sp = reinterpret_cast<const uint8_t*>(s.c_str());
        plain.insert(plain.end(), sp, sp + len * sizeof(wchar_t));
    };
    appendStr(username);
    appendStr(password);
    appendStr(userSid);

    DATA_BLOB in{static_cast<DWORD>(plain.size()), plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"DynamicIsland.FaceCredential", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        SecureZeroMemory(plain.data(), plain.size());
        return false;
    }

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LocalFree(out.pbData);
        SecureZeroMemory(plain.data(), plain.size());
        return false;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, out.pbData, out.cbData, &wrote, nullptr);
    CloseHandle(h);
    LocalFree(out.pbData);
    SecureZeroMemory(plain.data(), plain.size());
    return ok && wrote == out.cbData;
}

bool LoadDpapiRecord(std::wstring& outUsername, std::wstring& outPassword,
                     std::wstring& outUserSid) {
    outUsername.clear();
    outPassword.clear();
    outUserSid.clear();

    std::wstring path = CredentialPasswordStore::Path();
    if (path.empty()) return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart == 0) { CloseHandle(h); return false; }
    std::vector<uint8_t> blob(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    ReadFile(h, blob.data(), static_cast<DWORD>(blob.size()), &got, nullptr);
    CloseHandle(h);
    if (got != blob.size()) return false;

    DATA_BLOB in{static_cast<DWORD>(blob.size()), blob.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return false;
    }
    std::vector<uint8_t> plain(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);

    auto readStr = [&](size_t& off, std::wstring& s) -> bool {
        if (off + 4 > plain.size()) return false;
        uint32_t len = 0;
        memcpy(&len, plain.data() + off, 4);
        off += 4;
        if (len == 0 || off + len * sizeof(wchar_t) > plain.size()) return false;
        s.assign(reinterpret_cast<const wchar_t*>(plain.data() + off), len - 1);
        off += len * sizeof(wchar_t);
        return true;
    };

    size_t off = 0;
    bool ok = readStr(off, outUsername) && readStr(off, outPassword);
    if (ok && off < plain.size()) {
        std::wstring sid;
        if (readStr(off, sid)) {
            outUserSid = sid;
        }
    }
    SecureZeroMemory(plain.data(), plain.size());
    return ok;
}

} // namespace

std::wstring CredentialPasswordStore::Path() {
    wchar_t* pd = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) return L"";
    fs::path p(pd);
    CoTaskMemFree(pd);
    p /= L"DynamicIsland";
    std::error_code ec;
    fs::create_directories(p, ec);
    p /= L"face_cred.bin";
    return p.wstring();
}

bool CredentialPasswordStore::Save(const std::wstring& username, const std::wstring& password) {
    return Save(username, password, L"");
}

bool CredentialPasswordStore::Save(const std::wstring& username, const std::wstring& password,
                                   const std::wstring& userSid) {
    if (StoreLsaPassword(password)) {
        if (SaveDpapiRecord(username, L"", userSid)) {
            return true;
        }
        DeleteLsaPassword();
        return false;
    }

    // Non-elevated settings processes normally land here. The elevated
    // installer immediately migrates this temporary DPAPI password into LSA.
    return SaveDpapiRecord(username, password, userSid);
}

bool CredentialPasswordStore::Load(std::wstring& outUsername, std::wstring& outPassword) {
    std::wstring ignoredSid;
    return Load(outUsername, outPassword, ignoredSid);
}

bool CredentialPasswordStore::Load(std::wstring& outUsername, std::wstring& outPassword,
                                   std::wstring& outUserSid) {
    if (!LoadDpapiRecord(outUsername, outPassword, outUserSid)) {
        return false;
    }

    std::wstring lsaPassword;
    if (LoadLsaPassword(lsaPassword) && !lsaPassword.empty()) {
        SecureWString(outPassword);
        outPassword = std::move(lsaPassword);
        return true;
    }

    // Backward compatibility for old machine-DPAPI blobs and for the brief
    // pre-migration window before the elevated installer runs.
    return !outPassword.empty();
}

bool CredentialPasswordStore::Clear() {
    std::wstring path = Path();
    if (path.empty()) return false;
    BOOL ok = DeleteFileW(path.c_str());
    bool fileOk = ok || GetLastError() == ERROR_FILE_NOT_FOUND;

    // Best effort when called unelevated; the elevated uninstaller calls the
    // same method and removes the LSA secret.
    DeleteLsaPassword();
    return fileOk;
}

bool CredentialPasswordStore::MigrateToLsaSecret() {
    std::wstring username;
    std::wstring password;
    std::wstring sid;
    if (!LoadDpapiRecord(username, password, sid)) {
        return false;
    }

    if (password.empty()) {
        std::wstring existingLsaPassword;
        if (LoadLsaPassword(existingLsaPassword) && !existingLsaPassword.empty()) {
            SecureWString(existingLsaPassword);
            return SaveDpapiRecord(username, L"", sid);
        }
        return false;
    }

    bool ok = StoreLsaPassword(password);
    SecureWString(password);
    if (!ok) {
        return false;
    }

    return SaveDpapiRecord(username, L"", sid);
}

std::wstring CredentialPasswordStore::CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return L"";
    }

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    if (needed == 0) {
        CloseHandle(token);
        return L"";
    }

    std::vector<uint8_t> buffer(needed);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
        CloseHandle(token);
        return L"";
    }
    CloseHandle(token);

    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText)) {
        return L"";
    }

    std::wstring result(sidText);
    LocalFree(sidText);
    return result;
}

} // namespace face_core
