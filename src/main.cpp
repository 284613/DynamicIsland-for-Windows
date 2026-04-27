//main.cpp
#include <windows.h>
#include "DynamicIsland.h"
#include "CpInstaller.h"
#include <winrt/Windows.Foundation.h>
#include <ole2.h>
#include <shellapi.h>
#include <string>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    // Handle elevated sub-process arguments before any UI initialisation.
    // These are invoked by CpInstaller::Install / Uninstall via ShellExecuteEx runas.
    if (lpCmdLine && lpCmdLine[0]) {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (wcscmp(argv[i], L"--install-cp") == 0 && i + 1 < argc) {
                    int code = CpInstaller::RunInstallElevated(argv[i + 1]);
                    LocalFree(argv);
                    return code;
                }
                if (wcscmp(argv[i], L"--uninstall-cp") == 0) {
                    int code = CpInstaller::RunUninstallElevated();
                    LocalFree(argv);
                    return code;
                }
            }
            LocalFree(argv);
        }
    }

    // 保持这一行单线程初始化不变
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // 【关键修复】：强制初始化 OLE 子系统以支持拖放！
    OleInitialize(nullptr);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    DynamicIsland island;
    if (island.Initialize(hInstance)) {
        island.Run();
    }

    // 【新增】清理 OLE
    OleUninitialize();
    return 0;
}

