// WindowManager.h
#pragma once
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
#include "IMessageHandler.h"
class DropManager; // 在顶部添加前置声明
class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    bool Create(HINSTANCE hInstance, int width, int height, IMessageHandler* handler);
    void Show();
    void RunLoop();
    HWND GetHWND() const { return m_hwnd; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd;
    DropManager* m_dropManager = nullptr;
};


