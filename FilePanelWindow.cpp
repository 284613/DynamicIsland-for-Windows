// FilePanelWindow.cpp
#include "FilePanelWindow.h"
#include "Messages.h"
#include <shellapi.h>
#include <windowsx.h>

const wchar_t CLASS_NAME[] = L"FilePanelWindowClass";
const float ITEM_HEIGHT = 48.0f;
const int PANEL_WIDTH = 340;  // Same as expanded width
const int PANEL_HEIGHT = 240;  // ~2x expanded height
const int DELETE_BTN_SIZE = 28;
const int DELETE_BTN_RIGHT_MARGIN = 12;
const int CORNER_RADIUS = 16;
const int PANEL_PADDING = 16;

struct FileItemState {
    bool hovered = false;
    bool deleteHovered = false;
};

static std::vector<FileItemState> g_itemStates;

FilePanelWindow::FilePanelWindow() {}

FilePanelWindow::~FilePanelWindow() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClass(CLASS_NAME, GetModuleHandle(nullptr));
    }
}

bool FilePanelWindow::Create(HINSTANCE hInstance, HWND parentHwnd) {
    m_hInstance = hInstance;
    m_parentHwnd = parentHwnd;

    WNDCLASS wc = {};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.style = CS_VREDRAW | CS_HREDRAW;
    // No background brush - we handle everything
    RegisterClass(&wc);

    // Frameless layered window - no border
    m_hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST,
        CLASS_NAME, L"FilePanel",
        WS_POPUP,
        0, 0, PANEL_WIDTH, PANEL_HEIGHT,
        nullptr, nullptr, hInstance, this
    );

    if (m_hwnd) {
        // Use magenta as color key (transparent), alpha for rest
        SetLayeredWindowAttributes(m_hwnd, RGB(255, 0, 255), 220, LWA_COLORKEY | LWA_ALPHA);
        
        // Set rounded region for corners
        HRGN rgn = CreateRoundRectRgn(0, 0, PANEL_WIDTH + 1, PANEL_HEIGHT + 1, CORNER_RADIUS, CORNER_RADIUS);
        SetWindowRgn(m_hwnd, rgn, TRUE);
    }

    return m_hwnd != nullptr;
}

void FilePanelWindow::Show() {
    if (m_hwnd) {
        m_visible = true;
        ShowWindow(m_hwnd, SW_SHOWNA);
    }
}

void FilePanelWindow::Hide() {
    if (m_hwnd && m_visible) {
        m_visible = false;
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void FilePanelWindow::UpdateFiles(const std::vector<std::wstring>& files) {
    m_files = files;
    g_itemStates.resize(files.size());
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void FilePanelWindow::UpdatePosition(int x, int y, float height) {
    if (!m_hwnd) return;
    // Move left by 40, down by 10
    SetWindowPos(m_hwnd, nullptr, x - 40, y + 10, PANEL_WIDTH, PANEL_HEIGHT, SWP_NOZORDER | SWP_NOACTIVATE);
}

void FilePanelWindow::NotifyFileRemoved(int index) {
    if (m_parentHwnd) {
        SendMessage(m_parentHwnd, WM_FILE_REMOVED, index, 0);
    }
}

// Hit test - precise hit detection based on visual areas
int HitTestItem(int x, int y, size_t fileCount) {
    if (fileCount == 0) return -1;
    
    // Files start at y = 8
    if (y < 8) return -1;
    
    int index = (y - 8) / (int)ITEM_HEIGHT;
    if (index < 0 || index >= (int)fileCount) return -1;
    
    // Check which area: filename vs X button
    // Filename area: PANEL_PADDING to deleteBtnLeft
    // X button area: deleteBtnLeft to PANEL_WIDTH
    int deleteBtnLeft = PANEL_WIDTH - DELETE_BTN_SIZE - DELETE_BTN_RIGHT_MARGIN;
    
    if (x >= deleteBtnLeft && x < PANEL_WIDTH) {
        // Click on X button area
        return -2;
    } else if (x >= PANEL_PADDING && x < deleteBtnLeft) {
        // Click on filename area
        return index;
    }
    // Click outside valid areas
    return -1;
}

// Hover test - precise hover detection
void HitTestHover(int x, int y, size_t fileCount, std::vector<FileItemState>& states) {
    for (auto& s : states) {
        s.hovered = false;
        s.deleteHovered = false;
    }
    
    if (fileCount == 0) return;
    
    if (y < 8) return;
    
    int index = (y - 8) / (int)ITEM_HEIGHT;
    if (index < 0 || index >= (int)fileCount) return;
    
    int deleteBtnLeft = PANEL_WIDTH - DELETE_BTN_SIZE - DELETE_BTN_RIGHT_MARGIN;
    
    if (x >= deleteBtnLeft && x < PANEL_WIDTH) {
        states[index].deleteHovered = true;
    } else if (x >= PANEL_PADDING && x < deleteBtnLeft) {
        states[index].hovered = true;
    }
}

LRESULT CALLBACK FilePanelWindow::StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    FilePanelWindow* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        self = (FilePanelWindow*)((LPCREATESTRUCT)lParam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (FilePanelWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    return self ? self->WndProc(hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT FilePanelWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            // Force repaint on create
            SetTimer(hwnd, 1, 50, nullptr);
            return 0;
        
        case WM_TIMER:
            if (wParam == 1) {
                KillTimer(hwnd, 1);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
            
        case WM_NCPAINT:
            // Handle non-client area (borders) ourselves
            return 0;
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            // Magenta background (will be transparent), dark overlay
            HBRUSH magBrush = CreateSolidBrush(RGB(255, 0, 255));
            FillRect(hdc, &rc, magBrush);
            DeleteObject(magBrush);
            
            // Dark semi-transparent overlay
            HBRUSH darkBrush = CreateSolidBrush(RGB(25, 25, 25));
            FillRect(hdc, &rc, darkBrush);
            DeleteObject(darkBrush);

            SetBkMode(hdc, TRANSPARENT);
            
            // Title font
            HFONT titleFont = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            // File name font
            HFONT fileFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            
            // No title - just files
            
            // File list
            SelectObject(hdc, fileFont);
            int deleteBtnLeft = PANEL_WIDTH - DELETE_BTN_SIZE - DELETE_BTN_RIGHT_MARGIN;
            
            for (size_t i = 0; i < m_files.size(); ++i) {
                float itemY = 8.0f + i * ITEM_HEIGHT;
                
                // Item background with rounded corners
                RECT itemRect = {PANEL_PADDING / 2, (int)itemY, deleteBtnLeft - 4, (int)(itemY + ITEM_HEIGHT - 4)};
                
                // Hover background
                if (i < g_itemStates.size() && g_itemStates[i].hovered) {
                    HBRUSH hoverBrush = CreateSolidBrush(RGB(40, 40, 40));
                    FillRect(hdc, &itemRect, hoverBrush);
                    DeleteObject(hoverBrush);
                    SetTextColor(hdc, RGB(255, 255, 255));
                } else {
                    SetTextColor(hdc, RGB(200, 200, 200));
                }
                
                // File name
                std::wstring& path = m_files[i];
                size_t pos = path.find_last_of(L"\\/");
                std::wstring name = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
                
                RECT textRect = {PANEL_PADDING, (int)itemY + 2, deleteBtnLeft - 4, (int)(itemY + ITEM_HEIGHT - 2)};
                DrawText(hdc, name.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
                
                // Delete button (X)
                int btnY = (int)(itemY + (ITEM_HEIGHT - DELETE_BTN_SIZE) / 2);
                RECT closeRect = {deleteBtnLeft, btnY, deleteBtnLeft + DELETE_BTN_SIZE, btnY + DELETE_BTN_SIZE};
                
                // Delete button background - circle style
                bool deleteHover = (i < g_itemStates.size() && g_itemStates[i].deleteHovered);
                if (deleteHover) {
                    HBRUSH delBrush = CreateSolidBrush(RGB(200, 50, 50));
                    FillRect(hdc, &closeRect, delBrush);
                    DeleteObject(delBrush);
                }
                
                // X text
                SetTextColor(hdc, deleteHover ? RGB(255, 255, 255) : RGB(120, 120, 120));
                DrawText(hdc, L"x", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            
            DeleteObject(titleFont);
            DeleteObject(fileFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            
            // Save old states
            std::vector<FileItemState> oldStates = g_itemStates;
            
            HitTestHover(x, y, m_files.size(), g_itemStates);
            
            // Only redraw if state actually changed
            bool changed = (oldStates.size() != g_itemStates.size());
            if (!changed) {
                for (size_t i = 0; i < oldStates.size() && i < g_itemStates.size(); i++) {
                    if (oldStates[i].hovered != g_itemStates[i].hovered || 
                        oldStates[i].deleteHovered != g_itemStates[i].deleteHovered) {
                        changed = true;
                        break;
                    }
                }
            }
            
            if (changed) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_MOUSELEAVE: {
            for (auto& s : g_itemStates) {
                s.hovered = false;
                s.deleteHovered = false;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            
            int hit = HitTestItem(x, y, m_files.size());
            
            if (hit == -2) {
                int idx = (y - 32) / (int)ITEM_HEIGHT;
                if (idx >= 0 && idx < (int)m_files.size()) {
                    NotifyFileRemoved(idx);
                }
            } else if (hit >= 0) {
                ShellExecuteW(nullptr, L"open", m_files[hit].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
        
        case WM_SETCURSOR: {
            // Keep hand cursor
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


