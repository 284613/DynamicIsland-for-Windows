#pragma once
#include <string>

namespace face_core {

// Store for the Windows logon password used by the Credential Provider DLL.
// The main app writes an initial machine-scope DPAPI record at "Enable" time;
// the elevated installer migrates the password into an LSA private secret and
// leaves only username/SID metadata in the DPAPI file.
//
// Metadata path: %PROGRAMDATA%\DynamicIsland\face_cred.bin
// LSA secret: L$DynamicIsland.FaceUnlock.Password
class CredentialPasswordStore {
public:
    static bool Save(const std::wstring& username, const std::wstring& password);
    static bool Save(const std::wstring& username, const std::wstring& password,
                     const std::wstring& userSid);
    static bool Load(std::wstring& outUsername, std::wstring& outPassword);
    static bool Load(std::wstring& outUsername, std::wstring& outPassword,
                     std::wstring& outUserSid);
    static bool Clear();
    static bool MigrateToLsaSecret();
    static std::wstring Path();
    static std::wstring CurrentUserSid();
};

} // namespace face_core
