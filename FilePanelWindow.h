// FilePanelWindow.h
#pragma once
#include <windows.h>
#include <vector>
#include <string>

class FilePanelWindow {
public:
    FilePanelWindow();
    ~FilePanelWindow();

    bool Create(HINSTANCE hInstance, HWND parentHwnd);
    void Show();
    void Hide();
    void UpdateFiles(const std::vector<std::wstring>& files);
    void UpdatePosition(int x, int y, float height);
    HWND GetHWND() const { return m_hwnd; }
    bool IsVisible() const { return m_visible; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void NotifyFileRemoved(int index);

    HWND m_hwnd = nullptr;
    HWND m_parentHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    bool m_visible = false;
    std::vector<std::wstring> m_files;
};


