#define INITGUID
#include <windows.h>
#include <unknwn.h>
#include <shlwapi.h>
#include <delayimp.h>
#include <filesystem>
#include <new>
#include <string>

#include "Guids.h"
#include "CFaceCredentialProvider.h"

#pragma comment(lib, "shlwapi.lib")

static LONG    g_dllRefs  = 0;
static HMODULE g_hModule  = nullptr;

// ──────────────────────────────────────────
// Delay-load hook: redirect onnxruntime.dll to our own directory so the DLL
// search succeeds when we are loaded from %PROGRAMDATA%\DynamicIsland\.
// ──────────────────────────────────────────

static FARPROC WINAPI DelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary && pdli &&
        _stricmp(pdli->szDll, "onnxruntime.dll") == 0) {
        wchar_t dllPath[MAX_PATH] = {};
        GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
        std::wstring dir(dllPath);
        auto slash = dir.rfind(L'\\');
        if (slash != std::wstring::npos) dir.resize(slash + 1);
        dir += L"onnxruntime.dll";
        HMODULE h = LoadLibraryW(dir.c_str());
        if (h) return reinterpret_cast<FARPROC>(h);
    }
    return nullptr;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DelayLoadHook;

// ──────────────────────────────────────────
// DLL entry point
// ──────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

// ──────────────────────────────────────────
// COM exports
// ──────────────────────────────────────────

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_FaceCredentialProvider) return CLASS_E_CLASSNOTAVAILABLE;

    // Inline factory — avoids a separate header.
    class Factory : public IClassFactory {
    public:
        ULONG   AddRef()  { return InterlockedIncrement(&m_ref); }
        ULONG   Release() { ULONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }
        HRESULT QueryInterface(REFIID r, void** ppv) {
            if (r == IID_IUnknown || r == IID_IClassFactory) {
                *ppv = static_cast<IClassFactory*>(this);
                AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        HRESULT CreateInstance(IUnknown* outer, REFIID riid, void** ppv) {
            if (outer) return CLASS_E_NOAGGREGATION;
            return FaceCP::CFaceCredentialProvider::CreateInstance(riid, ppv);
        }
        HRESULT LockServer(BOOL lock) {
            if (lock) InterlockedIncrement(&g_dllRefs);
            else      InterlockedDecrement(&g_dllRefs);
            return S_OK;
        }
    private:
        LONG m_ref{1};
    };

    auto* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return (g_dllRefs == 0) ? S_OK : S_FALSE;
}

// ──────────────────────────────────────────
// Registration helpers
// ──────────────────────────────────────────

namespace {

constexpr const wchar_t* kCpRootKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\"
    L"{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}";

constexpr const wchar_t* kClsidKey =
    L"CLSID\\{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}";

constexpr const wchar_t* kInprocKey =
    L"CLSID\\{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}\\InprocServer32";

constexpr const wchar_t* kProviderName = L"DynamicIsland Face Credential Provider";

HRESULT WriteRegString(HKEY root, LPCWSTR subkey, LPCWSTR value, LPCWSTR data) {
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(root, subkey, 0, nullptr, 0,
                             KEY_SET_VALUE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(key, value, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(data),
                       static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(r);
}

} // namespace

extern "C" HRESULT __stdcall DllRegisterServer() {
    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

    HRESULT hr = WriteRegString(HKEY_CLASSES_ROOT, kClsidKey, nullptr, kProviderName);
    if (FAILED(hr)) return hr;
    hr = WriteRegString(HKEY_CLASSES_ROOT, kInprocKey, nullptr, dllPath);
    if (FAILED(hr)) return hr;
    hr = WriteRegString(HKEY_CLASSES_ROOT, kInprocKey, L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) return hr;
    hr = WriteRegString(HKEY_LOCAL_MACHINE, kCpRootKey, nullptr, kProviderName);
    return hr;
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    RegDeleteTreeW(HKEY_CLASSES_ROOT, kClsidKey);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kCpRootKey);
    return S_OK;
}
