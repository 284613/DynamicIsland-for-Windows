#include "CFaceCredential.h"

#include "CpLogger.h"
#include "face_core/CredentialPasswordStore.h"

#include <ntsecapi.h>
#include <shlwapi.h>
#include <sddl.h>
#include <wincred.h>
#include <windows.h>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace FaceCP {

namespace {

// Pack a KERB_INTERACTIVE_UNLOCK_LOGON with relative string offsets, as
// required by ICredentialProviderCredential::GetSerialization. Caller must
// CoTaskMemFree the returned buffer.
HRESULT PackKerbLogon(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
                      LPCWSTR domain, LPCWSTR user, LPCWSTR pass,
                      BYTE** ppBuf, DWORD* pSize)
{
    if (!domain || !user || !pass || !ppBuf || !pSize) return E_INVALIDARG;

    DWORD cbDomain = static_cast<DWORD>(wcslen(domain) * sizeof(wchar_t));
    DWORD cbUser   = static_cast<DWORD>(wcslen(user)   * sizeof(wchar_t));
    DWORD cbPass   = static_cast<DWORD>(wcslen(pass)   * sizeof(wchar_t));
    DWORD cbTotal  = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON) + cbDomain + cbUser + cbPass;

    auto* buf = static_cast<BYTE*>(CoTaskMemAlloc(cbTotal));
    if (!buf) return E_OUTOFMEMORY;
    ZeroMemory(buf, cbTotal);

    auto* kiul = reinterpret_cast<KERB_INTERACTIVE_UNLOCK_LOGON*>(buf);
    auto* kil  = &kiul->Logon;
    kil->MessageType = (cpus == CPUS_UNLOCK_WORKSTATION)
                       ? KerbWorkstationUnlockLogon : KerbInteractiveLogon;

    BYTE* str = buf + sizeof(KERB_INTERACTIVE_UNLOCK_LOGON);

    // UNICODE_STRING Buffer holds a relative offset from the start of the packed
    // buffer, not a real pointer. LSA's unpacker adds the buffer base address.
    auto setStr = [&](UNICODE_STRING& us, LPCWSTR src, DWORD cb) {
        us.Length        = static_cast<USHORT>(cb);
        us.MaximumLength = static_cast<USHORT>(cb);
        us.Buffer        = reinterpret_cast<PWSTR>(static_cast<ULONG_PTR>(str - buf));
        if (cb > 0) {
            memcpy(str, src, cb);
        }
        str += cb;
    };
    setStr(kil->LogonDomainName, domain, cbDomain);
    setStr(kil->UserName, user,   cbUser);
    setStr(kil->Password, pass,   cbPass);

    *ppBuf  = buf;
    *pSize  = cbTotal;
    return S_OK;
}

void TrimNullTerminator(std::wstring& value) {
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
}

void SecureWString(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

HRESULT ProtectPasswordForSerialization(const std::wstring& password,
                                        std::wstring& protectedPassword) {
    protectedPassword.clear();
    if (password.empty()) {
        return E_INVALIDARG;
    }

    std::wstring mutablePassword = password;
    CRED_PROTECTION_TYPE protectionType = CredUnprotected;
    if (CredIsProtectedW(mutablePassword.data(), &protectionType) &&
        protectionType != CredUnprotected) {
        protectedPassword = password;
        SecureWString(mutablePassword);
        return S_OK;
    }

    DWORD protectedChars = 0;
    if (!CredProtectW(FALSE, mutablePassword.data(),
                      static_cast<DWORD>(mutablePassword.size() + 1),
                      nullptr, &protectedChars, nullptr)) {
        const DWORD error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER || protectedChars == 0) {
            SecureWString(mutablePassword);
            return HRESULT_FROM_WIN32(error);
        }
    }

    std::wstring buffer(protectedChars, L'\0');
    if (!CredProtectW(FALSE, mutablePassword.data(),
                      static_cast<DWORD>(mutablePassword.size() + 1),
                      buffer.data(), &protectedChars, nullptr)) {
        const DWORD error = GetLastError();
        SecureWString(buffer);
        SecureWString(mutablePassword);
        return HRESULT_FROM_WIN32(error);
    }

    buffer.resize(protectedChars);
    TrimNullTerminator(buffer);
    protectedPassword = std::move(buffer);
    SecureWString(mutablePassword);
    return S_OK;
}

bool ResolveAccountFromSid(const std::wstring& sidText,
                           std::wstring& domain,
                           std::wstring& user) {
    domain.clear();
    user.clear();
    if (sidText.empty()) {
        return false;
    }

    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(sidText.c_str(), &sid)) {
        return false;
    }

    DWORD userLen = 0;
    DWORD domainLen = 0;
    SID_NAME_USE use{};
    LookupAccountSidW(nullptr, sid, nullptr, &userLen, nullptr, &domainLen, &use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || userLen == 0) {
        LocalFree(sid);
        return false;
    }

    std::wstring resolvedUser(userLen, L'\0');
    std::wstring resolvedDomain(domainLen, L'\0');
    BOOL ok = LookupAccountSidW(nullptr, sid,
                                resolvedUser.data(), &userLen,
                                resolvedDomain.empty() ? nullptr : resolvedDomain.data(),
                                &domainLen, &use);
    LocalFree(sid);
    if (!ok) {
        return false;
    }

    resolvedUser.resize(userLen);
    resolvedDomain.resize(domainLen);
    TrimNullTerminator(resolvedUser);
    TrimNullTerminator(resolvedDomain);
    if (resolvedUser.empty()) {
        return false;
    }

    user = std::move(resolvedUser);
    domain = std::move(resolvedDomain);
    return true;
}

void SplitStoredUserName(const std::wstring& storedUser,
                         const std::wstring& sidText,
                         std::wstring& domain,
                         std::wstring& user) {
    domain.clear();
    user = storedUser;

    const size_t slash = storedUser.find(L'\\');
    if (slash != std::wstring::npos) {
        domain = storedUser.substr(0, slash);
        user = storedUser.substr(slash + 1);
        return;
    }

    if (ResolveAccountFromSid(sidText, domain, user)) {
        return;
    }

    if (storedUser.find(L'@') != std::wstring::npos) {
        domain.clear();
        user = storedUser;
        return;
    }

    wchar_t machineName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD machLen = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(machineName, &machLen)) {
        domain = machineName;
    }
}

HRESULT GetAuthPackage(LSA_HANDLE hLsa, ULONG& pkg, NTSTATUS& lookupStatus) {
    pkg = 0;
    lookupStatus = static_cast<NTSTATUS>(0xC0000001L);
    if (!hLsa) {
        return HRESULT_FROM_NT(lookupStatus);
    }

    char name[] = "Negotiate";
    LSA_STRING lsaName{static_cast<USHORT>(strlen(name)),
                       static_cast<USHORT>(strlen(name) + 1), name};
    lookupStatus = LsaLookupAuthenticationPackage(hLsa, &lsaName, &pkg);
    return HRESULT_FROM_NT(lookupStatus);
}

std::wstring FormatLogonFailure(NTSTATUS ntStatus, NTSTATUS ntSubStatus) {
    wchar_t buf[160] = {};
    swprintf_s(buf, L"密码验证失败 NTSTATUS=0x%08X Sub=0x%08X",
               static_cast<unsigned long>(ntStatus),
               static_cast<unsigned long>(ntSubStatus));
    return buf;
}

} // namespace

CFaceCredential::CFaceCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
                                 const std::wstring& displayName,
                                 const std::wstring& userSid,
                                 std::function<void()> onSuccess,
                                 std::function<void()> onAuthFailure)
    : m_cpus(cpus), m_displayName(displayName), m_userSid(userSid),
      m_onSuccess(std::move(onSuccess)),
      m_onAuthFailure(std::move(onAuthFailure))
{
    CpLog(L"Credential ctor cpus=%d display='%s' sidPresent=%d",
          static_cast<int>(m_cpus), m_displayName.c_str(), !m_userSid.empty());
}

CFaceCredential::~CFaceCredential() {
    m_orchestrator.Stop();
    if (m_events) { m_events->Release(); m_events = nullptr; }
}

ULONG CFaceCredential::AddRef()  { return InterlockedIncrement(&m_cRef); }
ULONG CFaceCredential::Release() {
    ULONG r = InterlockedDecrement(&m_cRef);
    if (r == 0) delete this;
    return r;
}

HRESULT CFaceCredential::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICredentialProviderCredential ||
        riid == IID_ICredentialProviderCredential2) {
        *ppv = static_cast<ICredentialProviderCredential2*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

HRESULT CFaceCredential::Advise(ICredentialProviderCredentialEvents* pcpce) {
    if (m_events) m_events->Release();
    m_events = pcpce;
    if (m_events) m_events->AddRef();
    return S_OK;
}

HRESULT CFaceCredential::UnAdvise() {
    m_orchestrator.Stop();
    if (m_events) { m_events->Release(); m_events = nullptr; }
    return S_OK;
}

HRESULT CFaceCredential::SetSelected(BOOL* pbAutoLogonWithDefault) {
    if (pbAutoLogonWithDefault) *pbAutoLogonWithDefault = FALSE;
    CpLog(L"SetSelected authFailed=%d succeeded=%d running=%d",
          m_authFailed.load(), m_orchestrator.HasSucceeded(), m_orchestrator.IsRunning());
    if (m_authFailed.load()) {
        if (m_events) {
            m_events->SetFieldString(
                this, FIELD_STATUS,
                L"Windows 密码验证失败，请在设置中重新录入账户密码后再试");
        }
        return S_OK;
    }
    if (m_orchestrator.HasSucceeded()) {
        if (pbAutoLogonWithDefault) *pbAutoLogonWithDefault = TRUE;
        return S_OK;
    }
    if (m_orchestrator.IsRunning()) {
        return S_OK;
    }
    // Start face recognition when the user selects this tile.
    CpLog(L"SetSelected starting scanner");
    m_orchestrator.Start(m_events, this, FIELD_STATUS, m_onSuccess);
    return S_OK;
}

HRESULT CFaceCredential::SetDeselected() {
    m_orchestrator.Stop();
    return S_OK;
}

HRESULT CFaceCredential::GetFieldState(DWORD dwFieldID,
                                       CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
                                       CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis)
{
    if (!pcpfs || !pcpfis) return E_INVALIDARG;
    *pcpfis = CPFIS_NONE;
    switch (dwFieldID) {
    case FIELD_TILE_IMAGE: *pcpfs = CPFS_DISPLAY_IN_BOTH; break;
    case FIELD_USERNAME:   *pcpfs = CPFS_DISPLAY_IN_BOTH; break;
    case FIELD_STATUS:     *pcpfs = CPFS_DISPLAY_IN_SELECTED_TILE; break;
    case FIELD_SUBMIT:     *pcpfs = CPFS_DISPLAY_IN_SELECTED_TILE; break;
    default:               return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT CFaceCredential::GetStringValue(DWORD dwFieldID, PWSTR* ppwsz) {
    if (!ppwsz) return E_INVALIDARG;
    *ppwsz = nullptr;
    switch (dwFieldID) {
    case FIELD_USERNAME: {
        return SHStrDupW(m_displayName.c_str(), ppwsz);
    }
    case FIELD_STATUS:
        if (m_authFailed.load()) {
            return SHStrDupW(L"Windows 密码验证失败，请在设置中重新录入账户密码后再试", ppwsz);
        }
        return SHStrDupW(L"正在准备摄像头...", ppwsz);
    default:
        return E_INVALIDARG;
    }
}

HRESULT CFaceCredential::GetBitmapValue(DWORD dwFieldID, HBITMAP* phbmp) {
    if (dwFieldID != FIELD_TILE_IMAGE || !phbmp) return E_INVALIDARG;
    *phbmp = nullptr;
    return S_OK;  // Use default tile image.
}

HRESULT CFaceCredential::GetSubmitButtonValue(DWORD dwFieldID, DWORD* pdwAdjacentTo) {
    if (dwFieldID != FIELD_SUBMIT || !pdwAdjacentTo) return E_INVALIDARG;
    *pdwAdjacentTo = FIELD_STATUS;
    return S_OK;
}

HRESULT CFaceCredential::GetSerialization(
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*   pcpcs,
    PWSTR*                                          ppwszOptionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON*                pcpsiOptionalStatusIcon)
{
    CpLog(L"GetSerialization begin succeeded=%d authFailed=%d",
          m_orchestrator.HasSucceeded(), m_authFailed.load());
    if (!pcpgsr || !pcpcs) return E_INVALIDARG;
    UNREFERENCED_PARAMETER(ppwszOptionalStatusText);
    UNREFERENCED_PARAMETER(pcpsiOptionalStatusIcon);

    if (!m_orchestrator.HasSucceeded()) {
        CpLog(L"GetSerialization no credential: face not finished");
        *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
        return S_OK;
    }

    std::wstring storedUser, storedPass;
    if (!face_core::CredentialPasswordStore::Load(storedUser, storedPass)) {
        CpLog(L"GetSerialization failed: CredentialPasswordStore::Load");
        *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
        if (ppwszOptionalStatusText)
            SHStrDupW(L"未找到存储的密码，请重新启用人脸解锁", ppwszOptionalStatusText);
        return E_FAIL;
    }

    std::wstring domain;
    std::wstring user;
    SplitStoredUserName(storedUser, m_userSid, domain, user);
    CpLog(L"GetSerialization storedUser='%s' resolvedDomain='%s' resolvedUser='%s' passwordChars=%zu sidPresent=%d",
          storedUser.c_str(), domain.c_str(), user.c_str(), storedPass.size(), !m_userSid.empty());
    if (user.empty()) {
        SecureWString(storedPass);
        *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
        if (ppwszOptionalStatusText)
            SHStrDupW(L"账户名无效，请重新启用人脸解锁", ppwszOptionalStatusText);
        return E_FAIL;
    }

    // Use Negotiate so local SAM, Microsoft account, and domain accounts are
    // routed to the right logon provider.
    LSA_HANDLE hLsa = nullptr;
    NTSTATUS lsaStatus = LsaConnectUntrusted(&hLsa);
    NTSTATUS lookupStatus = static_cast<NTSTATUS>(0xC0000001L);
    ULONG authPkg = 0;
    HRESULT authHr = GetAuthPackage(hLsa, authPkg, lookupStatus);
    if (hLsa) LsaDeregisterLogonProcess(hLsa);
    CpLog(L"GetSerialization LsaConnect=0x%08X lookup=0x%08X authHr=0x%08X authPkg=%lu",
          static_cast<unsigned long>(lsaStatus),
          static_cast<unsigned long>(lookupStatus),
          static_cast<unsigned long>(authHr),
          authPkg);
    if (FAILED(HRESULT_FROM_NT(lsaStatus)) || FAILED(authHr)) {
        SecureWString(storedPass);
        *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
        if (ppwszOptionalStatusText)
            SHStrDupW(L"无法打开 Windows 登录认证包", ppwszOptionalStatusText);
        return E_FAIL;
    }

    std::wstring protectedPass;
    HRESULT hr = ProtectPasswordForSerialization(storedPass, protectedPass);
    CpLog(L"GetSerialization protectPassword hr=0x%08X protectedChars=%zu",
          static_cast<unsigned long>(hr), protectedPass.size());
    SecureWString(storedPass);
    if (FAILED(hr)) {
        *pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
        if (ppwszOptionalStatusText)
            SHStrDupW(L"密码保护失败，请重新启用人脸解锁", ppwszOptionalStatusText);
        return hr;
    }

    BYTE*  buf     = nullptr;
    DWORD  bufSize = 0;
    hr = PackKerbLogon(m_cpus, domain.c_str(),
                       user.c_str(), protectedPass.c_str(),
                       &buf, &bufSize);
    CpLog(L"GetSerialization pack hr=0x%08X cb=%lu",
          static_cast<unsigned long>(hr), bufSize);
    SecureWString(protectedPass);

    if (FAILED(hr)) return hr;

    pcpcs->ulAuthenticationPackage = authPkg;
    pcpcs->rgbSerialization        = buf;
    pcpcs->cbSerialization         = bufSize;
    pcpcs->clsidCredentialProvider = CLSID_FaceCredentialProvider;
    *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
    CpLog(L"GetSerialization returning credential authPkg=%lu cb=%lu", authPkg, bufSize);
    return S_OK;
}

HRESULT CFaceCredential::ReportResult(NTSTATUS ntStatus, NTSTATUS ntSubStatus,
                                      PWSTR* ppwszOptionalStatusText,
                                      CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon)
{
    CpLog(L"ReportResult status=0x%08X sub=0x%08X",
          static_cast<unsigned long>(ntStatus),
          static_cast<unsigned long>(ntSubStatus));
    // Non-zero means logon failed — reset so the user can try again.
    if (FAILED(HRESULT_FROM_NT(ntStatus))) {
        const std::wstring message = FormatLogonFailure(ntStatus, ntSubStatus);
        m_authFailed = true;
        m_orchestrator.Stop();
        m_orchestrator.ResetSucceeded();
        if (m_onAuthFailure) {
            m_onAuthFailure();
        }
        if (m_events) {
            m_events->SetFieldString(this, FIELD_STATUS, message.c_str());
        }
        if (ppwszOptionalStatusText) {
            SHStrDupW(message.c_str(), ppwszOptionalStatusText);
        }
        if (pcpsiOptionalStatusIcon) {
            *pcpsiOptionalStatusIcon = CPSI_ERROR;
        }
    }
    return S_OK;
}

HRESULT CFaceCredential::GetUserSid(PWSTR* ppszSid) {
    if (!ppszSid) return E_INVALIDARG;
    *ppszSid = nullptr;
    if (m_userSid.empty()) {
        return S_FALSE;
    }
    return SHStrDupW(m_userSid.c_str(), ppszSid);
}

} // namespace FaceCP
