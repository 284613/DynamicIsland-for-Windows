#pragma once
#include <string>
#include <windows.h>

// Manages the FaceUnlockProvider Credential Provider registration.
// Registration requires elevation; this module handles both the elevated
// sub-process side (called from main.cpp for --install-cp / --uninstall-cp)
// and the caller side (ShellExecuteEx with runas from the settings toggle).
namespace CpInstaller {

// Returns true if the CP is currently registered in HKLM.
bool IsRegistered();

// Trigger elevated install: copy DLLs + models to PROGRAMDATA, run regsvr32.
// Blocks until the elevated helper exits. Returns true on success.
// sourceDir: directory containing FaceUnlockProvider.dll and onnxruntime.dll.
bool Install(HWND hwndParent, const std::wstring& sourceDir);

// Trigger elevated uninstall: run regsvr32 /u, delete DLL from PROGRAMDATA.
bool Uninstall(HWND hwndParent);

// Called from main() when argv contains --install-cp <sourceDir>.
// Runs elevated: copies files, calls regsvr32. Returns process exit code.
int RunInstallElevated(const std::wstring& sourceDir);

// Called from main() when argv contains --uninstall-cp.
int RunUninstallElevated();

} // namespace CpInstaller
