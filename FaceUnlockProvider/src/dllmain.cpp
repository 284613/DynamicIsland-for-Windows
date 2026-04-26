#define INITGUID
#include <windows.h>
#include <unknwn.h>

#include "Guids.h"

// Phase 0 scaffold: real ICredentialProvider class factory ships in Phase 3.
// These exports satisfy the .def file so the DLL builds and can be probed by
// regsvr32 for sanity, even though it does nothing yet.

static LONG g_dllRefs = 0;

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Disable per-thread notifications — we don't need them.
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID, REFIID, LPVOID* ppv) {
    if (ppv) *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;  // Phase 3 wires the real factory.
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return (g_dllRefs == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
    return S_OK;  // Phase 3 writes registry keys.
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    return S_OK;
}
