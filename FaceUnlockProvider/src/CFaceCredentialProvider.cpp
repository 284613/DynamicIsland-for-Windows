#include "CFaceCredentialProvider.h"

#include "CpLogger.h"
#include "face_core/CredentialPasswordStore.h"
#include "face_core/FaceTemplateStore.h"
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <mfapi.h>
#include <mfidl.h>
#include <filesystem>
#include <new>

#pragma comment(lib, "mfplat.lib")

namespace fs = std::filesystem;

namespace FaceCP {

namespace {

// Check for a connected video device via MF (fast, ~10 ms).
bool CameraAvailable() {
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    IMFAttributes* attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 1))) {
        MFShutdown();
        return false;
    }
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(attrs, &devices, &count);
    attrs->Release();
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
        CoTaskMemFree(devices);
    }
    MFShutdown();
    return SUCCEEDED(hr) && count > 0;
}

// Check that face templates and credential are ready.
bool ReadyToUnlock() {
    std::wstring sharedPath = face_core::FaceTemplateStore::SharedPath();
    std::error_code ec;
    if (!fs::exists(sharedPath, ec)) return false;

    std::wstring u, p;
    return face_core::CredentialPasswordStore::Load(u, p);
}

bool LoadStoredCredentialInfo(std::wstring& user, std::wstring& sid) {
    std::wstring pass;
    if (!face_core::CredentialPasswordStore::Load(user, pass, sid)) {
        return false;
    }
    if (!pass.empty()) {
        SecureZeroMemory(&pass[0], pass.size() * sizeof(wchar_t));
    }
    return true;
}

bool UserArrayContainsSid(ICredentialProviderUserArray* users, const std::wstring& sid) {
    if (!users || sid.empty()) {
        return true;
    }

    DWORD count = 0;
    if (FAILED(users->GetCount(&count))) {
        return true;
    }

    for (DWORD i = 0; i < count; ++i) {
        ICredentialProviderUser* user = nullptr;
        if (FAILED(users->GetAt(i, &user)) || !user) {
            continue;
        }

        PWSTR userSid = nullptr;
        HRESULT hr = user->GetSid(&userSid);
        user->Release();
        if (SUCCEEDED(hr) && userSid) {
            const bool matches = _wcsicmp(userSid, sid.c_str()) == 0;
            CoTaskMemFree(userSid);
            if (matches) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

// ──────────────────────────────────────────
// CFaceCredentialProvider
// ──────────────────────────────────────────

CFaceCredentialProvider::CFaceCredentialProvider() = default;

CFaceCredentialProvider::~CFaceCredentialProvider() {
    if (m_credential) { m_credential->Release(); m_credential = nullptr; }
}

ULONG CFaceCredentialProvider::AddRef()  { return InterlockedIncrement(&m_cRef); }
ULONG CFaceCredentialProvider::Release() {
    ULONG r = InterlockedDecrement(&m_cRef);
    if (r == 0) delete this;
    return r;
}

HRESULT CFaceCredentialProvider::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ICredentialProvider) {
        *ppv = static_cast<ICredentialProvider*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_ICredentialProviderSetUserArray) {
        *ppv = static_cast<ICredentialProviderSetUserArray*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

HRESULT CFaceCredentialProvider::SetUsageScenario(
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD /*dwFlags*/)
{
    CpLog(L"SetUsageScenario cpus=%d", static_cast<int>(cpus));
    m_cpus   = cpus;
    m_active = false;

    if (cpus != CPUS_LOGON && cpus != CPUS_UNLOCK_WORKSTATION) {
        CpLog(L"SetUsageScenario ignored cpus=%d", static_cast<int>(cpus));
        return E_NOTIMPL;
    }

    // Don't show the tile if no camera or no enrolled templates.
    const bool cameraOk = CameraAvailable();
    const bool ready = ReadyToUnlock();
    CpLog(L"SetUsageScenario readiness camera=%d ready=%d", cameraOk, ready);
    if (!cameraOk || !ready)
        return S_OK;

    std::wstring displayName;
    if (!LoadStoredCredentialInfo(displayName, m_storedSid)) {
        CpLog(L"SetUsageScenario failed: no stored credential info");
        return S_OK;
    }
    CpLog(L"SetUsageScenario stored display='%s' sidPresent=%d userArrayKnown=%d sidAllowed=%d",
          displayName.c_str(), !m_storedSid.empty(), m_userArrayKnown, m_userArrayAllowsStoredSid);
    if (m_userArrayKnown && !m_storedSid.empty() && !m_userArrayAllowsStoredSid) {
        CpLog(L"SetUsageScenario inactive: stored SID not in user array");
        return S_OK;
    }

    m_active    = true;
    m_autoLogon = false;

    // Create the credential object. Pass a success callback that sets m_autoLogon
    // and fires CredentialsChanged so LogonUI auto-submits.
    if (m_credential) { m_credential->Release(); m_credential = nullptr; }
    m_credential = new (std::nothrow) CFaceCredential(
        cpus, displayName.empty() ? L"人脸解锁" : displayName, m_storedSid,
        [this]() {
            CpLog(L"Provider success callback: enabling auto logon");
            m_autoLogon = true;
            if (m_cpEvents) m_cpEvents->CredentialsChanged(m_adviseContext);
        },
        [this]() {
            CpLog(L"Provider auth failure callback: disabling auto logon");
            m_autoLogon = false;
        });
    CpLog(L"SetUsageScenario active credentialCreated=%d", m_credential != nullptr);
    return m_credential ? S_OK : E_OUTOFMEMORY;
}

HRESULT CFaceCredentialProvider::SetUserArray(ICredentialProviderUserArray* users) {
    CpLog(L"SetUserArray begin users=%p", users);
    m_userArrayKnown = true;

    std::wstring displayName;
    std::wstring sid;
    if (!LoadStoredCredentialInfo(displayName, sid) || sid.empty()) {
        m_userArrayAllowsStoredSid = true;
        return S_OK;
    }

    m_storedSid = sid;
    m_userArrayAllowsStoredSid = UserArrayContainsSid(users, sid);
    CpLog(L"SetUserArray stored display='%s' sidPresent=%d allowed=%d active=%d",
          displayName.c_str(), !sid.empty(), m_userArrayAllowsStoredSid, m_active);
    if (!m_userArrayAllowsStoredSid && m_active) {
        if (m_credential) { m_credential->Release(); m_credential = nullptr; }
        m_active = false;
        m_autoLogon = false;
        if (m_cpEvents) {
            m_cpEvents->CredentialsChanged(m_adviseContext);
        }
    }
    return S_OK;
}

HRESULT CFaceCredentialProvider::SetSerialization(
    const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* /*pcpcs*/)
{
    return E_NOTIMPL;
}

HRESULT CFaceCredentialProvider::Advise(ICredentialProviderEvents* pcpe,
                                         UINT_PTR upAdviseContext)
{
    CpLog(L"Provider Advise events=%p context=%llu", pcpe,
          static_cast<unsigned long long>(upAdviseContext));
    if (m_cpEvents) { m_cpEvents->Release(); m_cpEvents = nullptr; }
    m_cpEvents = pcpe;
    if (m_cpEvents) m_cpEvents->AddRef();
    m_adviseContext = upAdviseContext;
    return S_OK;
}

HRESULT CFaceCredentialProvider::UnAdvise() {
    CpLog(L"Provider UnAdvise");
    if (m_cpEvents) { m_cpEvents->Release(); m_cpEvents = nullptr; }
    m_adviseContext = 0;
    return S_OK;
}

HRESULT CFaceCredentialProvider::GetFieldDescriptorCount(DWORD* pdwCount) {
    if (!pdwCount) return E_INVALIDARG;
    *pdwCount = FIELD_COUNT;
    return S_OK;
}

HRESULT CFaceCredentialProvider::GetFieldDescriptorAt(
    DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd)
{
    if (!ppcpfd) return E_INVALIDARG;
    *ppcpfd = nullptr;

    auto* desc = static_cast<CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*>(
        CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)));
    if (!desc) return E_OUTOFMEMORY;
    ZeroMemory(desc, sizeof(*desc));
    desc->dwFieldID = dwIndex;

    HRESULT hr = S_OK;
    switch (dwIndex) {
    case FIELD_TILE_IMAGE:
        desc->cpft       = CPFT_TILE_IMAGE;
        hr = SHStrDupW(L"人脸解锁图标", &desc->pszLabel);
        break;
    case FIELD_USERNAME:
        desc->cpft       = CPFT_LARGE_TEXT;
        hr = SHStrDupW(L"用户名", &desc->pszLabel);
        break;
    case FIELD_STATUS:
        desc->cpft       = CPFT_SMALL_TEXT;
        hr = SHStrDupW(L"状态", &desc->pszLabel);
        break;
    case FIELD_SUBMIT:
        desc->cpft       = CPFT_SUBMIT_BUTTON;
        hr = SHStrDupW(L"提交", &desc->pszLabel);
        break;
    default:
        CoTaskMemFree(desc);
        return E_INVALIDARG;
    }

    if (FAILED(hr)) {
        CoTaskMemFree(desc);
        return hr;
    }
    *ppcpfd = desc;
    return S_OK;
}

HRESULT CFaceCredentialProvider::GetCredentialCount(DWORD* pdwCount, DWORD* pdwDefault,
                                                     BOOL* pbAutoLogonWithDefault)
{
    if (!pdwCount || !pdwDefault || !pbAutoLogonWithDefault) return E_INVALIDARG;
    CpLog(L"GetCredentialCount active=%d credential=%p autoLogon=%d",
          m_active, m_credential, m_autoLogon);
    if (m_active && m_credential) {
        *pdwCount                = 1;
        *pdwDefault              = 0;
        *pbAutoLogonWithDefault  = m_autoLogon ? TRUE : FALSE;
    } else {
        *pdwCount                = 0;
        *pdwDefault              = CREDENTIAL_PROVIDER_NO_DEFAULT;
        *pbAutoLogonWithDefault  = FALSE;
    }
    return S_OK;
}

HRESULT CFaceCredentialProvider::GetCredentialAt(DWORD dwIndex,
                                                  ICredentialProviderCredential** ppcpc)
{
    CpLog(L"GetCredentialAt index=%lu credential=%p", dwIndex, m_credential);
    if (!ppcpc) return E_INVALIDARG;
    *ppcpc = nullptr;
    if (dwIndex != 0 || !m_credential) return E_INVALIDARG;
    m_credential->AddRef();
    *ppcpc = m_credential;
    return S_OK;
}

// static
HRESULT CFaceCredentialProvider::CreateInstance(REFIID riid, void** ppv) {
    auto* p = new (std::nothrow) CFaceCredentialProvider();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}

} // namespace FaceCP
