// WindowManager.cpp
#include "WindowManager.h"
#include "DropManager.h"
WindowManager::WindowManager() : m_hwnd(nullptr), m_dropManager(nullptr) {}

WindowManager::~WindowManager() {
    if (m_dropManager) {
        // 先注销拖拽，再释放管理器
        if (m_hwnd) {
            RevokeDragDrop(m_hwnd);
        }
        delete m_dropManager;
        m_dropManager = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool WindowManager::Create(HINSTANCE hInstance, int width, int height, IMessageHandler* handler) {
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = StaticWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"DynamicIslandClass";

    if (!GetClassInfoEx(hInstance, L"DynamicIslandClass", &wcex))
    {
        RegisterClassEx(&wcex);
    }

    // 屏幕居中顶部计算
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int posX = (screenWidth - width) / 2;
    int posY = 0;

    DWORD dwExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    DWORD dwStyle = WS_POPUP;

    m_hwnd = CreateWindowEx(
        dwExStyle, L"DynamicIslandClass", L"DynamicIslandWindow",
        dwStyle, posX, posY, width, height,
        nullptr, nullptr, hInstance, handler // 传入 handler 作为 lpParam
    );
    if (m_hwnd) {
        m_dropManager = new DropManager(m_hwnd);
        RegisterDragDrop(m_hwnd, m_dropManager);
    }

    return m_hwnd != nullptr;
}

void WindowManager::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNA);
        UpdateWindow(m_hwnd);
    }
}

void WindowManager::RunLoop() {
    MSG msg = { 0 };
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK WindowManager::StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    IMessageHandler* pHandler = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pHandler = (IMessageHandler*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pHandler);
    }
    else {
        pHandler = (IMessageHandler*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (pHandler) {
        return pHandler->HandleMessage(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

