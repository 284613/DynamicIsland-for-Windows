#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <windows.h>
#include <shlwapi.h>
#include <credentialprovider.h>
#include "Guids.h"
#include "UnlockOrchestrator.h"

namespace FaceCP {

// Field indices — must match the schema declared in CFaceCredentialProvider.
enum : DWORD {
    FIELD_TILE_IMAGE = 0,
    FIELD_USERNAME   = 1,
    FIELD_STATUS     = 2,
    FIELD_SUBMIT     = 3,
    FIELD_COUNT      = 4,
};

class CFaceCredential : public ICredentialProviderCredential2 {
public:
    // onSuccess: called (on the orchestrator thread) when face recognition succeeds.
    // The provider passes a lambda that fires ICredentialProviderEvents::CredentialsChanged.
    CFaceCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
                    const std::wstring& displayName,
                    const std::wstring& userSid,
                    std::function<void()> onSuccess,
                    std::function<void()> onAuthFailure);
    virtual ~CFaceCredential();

    // IUnknown
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    // ICredentialProviderCredential
    HRESULT STDMETHODCALLTYPE Advise(ICredentialProviderCredentialEvents* pcpce) override;
    HRESULT STDMETHODCALLTYPE UnAdvise() override;
    HRESULT STDMETHODCALLTYPE SetSelected(BOOL* pbAutoLogonWithDefault) override;
    HRESULT STDMETHODCALLTYPE SetDeselected() override;
    HRESULT STDMETHODCALLTYPE GetFieldState(
        DWORD dwFieldID,
        CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
        CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis) override;
    HRESULT STDMETHODCALLTYPE GetStringValue(DWORD dwFieldID, PWSTR* ppwsz) override;
    HRESULT STDMETHODCALLTYPE GetBitmapValue(DWORD dwFieldID, HBITMAP* phbmp) override;
    HRESULT STDMETHODCALLTYPE GetCheckboxValue(DWORD, BOOL*, PWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSubmitButtonValue(DWORD, DWORD* pdwAdjacentTo) override;
    HRESULT STDMETHODCALLTYPE GetComboBoxValueCount(DWORD, DWORD*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetComboBoxValueAt(DWORD, DWORD, PWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStringValue(DWORD, PCWSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetCheckboxValue(DWORD, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetComboBoxSelectedValue(DWORD, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CommandLinkClicked(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSerialization(
        CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
        CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
        PWSTR* ppwszOptionalStatusText,
        CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon) override;
    HRESULT STDMETHODCALLTYPE ReportResult(
        NTSTATUS, NTSTATUS, PWSTR*, CREDENTIAL_PROVIDER_STATUS_ICON*) override;

    // ICredentialProviderCredential2
    HRESULT STDMETHODCALLTYPE GetUserSid(PWSTR* ppszSid) override;

private:
    LONG                                  m_cRef{1};
    CREDENTIAL_PROVIDER_USAGE_SCENARIO    m_cpus;
    std::wstring                          m_displayName;
    std::wstring                          m_userSid;
    std::function<void()>                 m_onSuccess;
    std::function<void()>                 m_onAuthFailure;
    ICredentialProviderCredentialEvents*  m_events{nullptr};
    UnlockOrchestrator                    m_orchestrator;
    std::atomic<bool>                     m_authFailed{false};
};

} // namespace FaceCP
