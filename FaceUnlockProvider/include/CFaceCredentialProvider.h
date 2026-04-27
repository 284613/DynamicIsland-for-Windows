#pragma once
#include <windows.h>
#include <credentialprovider.h>
#include "Guids.h"
#include "CFaceCredential.h"

namespace FaceCP {

class CFaceCredentialProvider : public ICredentialProvider, public ICredentialProviderSetUserArray {
public:
    CFaceCredentialProvider();
    virtual ~CFaceCredentialProvider();

    // IUnknown
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    // ICredentialProvider
    HRESULT STDMETHODCALLTYPE SetUsageScenario(
        CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD dwFlags) override;
    HRESULT STDMETHODCALLTYPE SetSerialization(
        const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs) override;
    HRESULT STDMETHODCALLTYPE Advise(
        ICredentialProviderEvents* pcpe, UINT_PTR upAdviseContext) override;
    HRESULT STDMETHODCALLTYPE UnAdvise() override;
    HRESULT STDMETHODCALLTYPE GetFieldDescriptorCount(DWORD* pdwCount) override;
    HRESULT STDMETHODCALLTYPE GetFieldDescriptorAt(
        DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd) override;
    HRESULT STDMETHODCALLTYPE GetCredentialCount(
        DWORD* pdwCount, DWORD* pdwDefault, BOOL* pbAutoLogonWithDefault) override;
    HRESULT STDMETHODCALLTYPE GetCredentialAt(
        DWORD dwIndex, ICredentialProviderCredential** ppcpc) override;

    // ICredentialProviderSetUserArray
    HRESULT STDMETHODCALLTYPE SetUserArray(ICredentialProviderUserArray* users) override;

    // Class factory helper
    static HRESULT CreateInstance(REFIID riid, void** ppv);

private:
    LONG                               m_cRef{1};
    CREDENTIAL_PROVIDER_USAGE_SCENARIO m_cpus{CPUS_INVALID};
    CFaceCredential*                   m_credential{nullptr};
    bool                               m_active{false};   // scenario + templates ok
    bool                               m_autoLogon{false}; // set to true on face success
    bool                               m_userArrayKnown{false};
    bool                               m_userArrayAllowsStoredSid{true};
    std::wstring                       m_storedSid;

    // Provider-level events (from Advise) used to fire CredentialsChanged.
    ICredentialProviderEvents*         m_cpEvents{nullptr};
    UINT_PTR                           m_adviseContext{0};
};

} // namespace FaceCP
