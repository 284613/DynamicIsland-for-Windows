//main.cpp
#include <windows.h>
#include "DynamicIsland.h"
#include <winrt/Windows.Foundation.h>
#include <ole2.h> // 【新增】

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
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

