#include "SettingsWindow.h"
#include <stdio.h>
#include <windowsx.h>

const wchar_t CLASS_NAME[] = L"SettingsWindowClass";
const wchar_t SETTINGS_FILE[] = L"DynamicIslandSettings.ini";

#define NAV_BUTTON_ID_BASE 1000
#define SLIDER_ID_BASE 2000
#define BUTTON_ID_BASE 3000

LRESULT CALLBACK SettingsWindow::StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = NULL;
    if (uMsg == WM_NCCREATE) {
        self = (SettingsWindow*)((LPCREATESTRUCT)lParam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (SettingsWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    return self ? self->WndProc(hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);
}

SettingsWindow::SettingsWindow() {}

SettingsWindow::~SettingsWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
}

void SettingsWindow::LoadSettings() {
    m_darkMode = (GetPrivateProfileInt(L"Settings", L"DarkMode", 0, SETTINGS_FILE) != 0);
    m_followSystemTheme = (GetPrivateProfileInt(L"Settings", L"FollowSystemTheme", 1, SETTINGS_FILE) != 0);
    m_mainUIWidth = (float)GetPrivateProfileInt(L"MainUI", L"Width", 400, SETTINGS_FILE);
    m_mainUIHeight = (float)GetPrivateProfileInt(L"MainUI", L"Height", 200, SETTINGS_FILE);
    m_mainUITransparency = GetPrivateProfileInt(L"MainUI", L"Transparency", 100, SETTINGS_FILE) / 100.0f;
    m_filePanelWidth = (float)GetPrivateProfileInt(L"FilePanel", L"Width", 340, SETTINGS_FILE);
    m_filePanelHeight = (float)GetPrivateProfileInt(L"FilePanel", L"Height", 240, SETTINGS_FILE);
    m_filePanelTransparency = GetPrivateProfileInt(L"FilePanel", L"Transparency", 90, SETTINGS_FILE) / 100.0f;

    if (m_followSystemTheme) {
        m_darkMode = GetSystemDarkMode();
    }
}

void SettingsWindow::SaveSettings() {
    wchar_t buf[32];
    swprintf_s(buf, L"%d", m_darkMode ? 1 : 0);
    WritePrivateProfileString(L"Settings", L"DarkMode", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", m_followSystemTheme ? 1 : 0);
    WritePrivateProfileString(L"Settings", L"FollowSystemTheme", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", (int)m_mainUIWidth);
    WritePrivateProfileString(L"MainUI", L"Width", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", (int)m_mainUIHeight);
    WritePrivateProfileString(L"MainUI", L"Height", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", (int)(m_mainUITransparency * 100));
    WritePrivateProfileString(L"MainUI", L"Transparency", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", (int)m_filePanelWidth);
    WritePrivateProfileString(L"FilePanel", L"Width", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", (int)m_filePanelHeight);
    WritePrivateProfileString(L"FilePanel", L"Height", buf, SETTINGS_FILE);
    swprintf_s(buf, L"%d", (int)(m_filePanelTransparency * 100));
    WritePrivateProfileString(L"FilePanel", L"Transparency", buf, SETTINGS_FILE);
}

void SettingsWindow::ApplySettings() {
    SaveSettings();
    ApplyTheme();
    if (m_parentHwnd) {
        PostMessage(m_parentHwnd, WM_USER + 200, 0, 0);
    }
}

bool SettingsWindow::GetSystemDarkMode() {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
        return value == 0;
    }
    return false;
}

void SettingsWindow::ApplyTheme() {
    if (!m_hwnd) return;
    InvalidateRect(m_hwnd, NULL, TRUE);
    UpdateWindow(m_hwnd);
}

void SettingsWindow::UpdateSliderValue(SliderControl& slider, int pos) {
    if (slider.valueHwnd) {
        std::wstring valStr = std::to_wstring(pos);
        if (&slider == &m_sliderMainTransparency || &slider == &m_sliderFileTransparency) {
            valStr += L"%";
        }
        SetWindowText(slider.valueHwnd, valStr.c_str());
    }
}

bool SettingsWindow::Create(HINSTANCE hInstance, HWND parentHwnd) {
    m_hInstance = hInstance;
    m_parentHwnd = parentHwnd;

    WNDCLASS wc = {};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClass(&wc);

    m_hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, CLASS_NAME, L"Dynamic Island Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, 
        parentHwnd, NULL, hInstance, this);

    if (!m_hwnd) return false;

    LoadSettings();
    CreateControls();
    return true;
}

void SettingsWindow::CreateControls() {
    CreateNavigationButtons();
    SwitchCategory(m_currentCategory);
}

void SettingsWindow::CreateNavigationButtons() {
    const std::vector<std::wstring> categories = {
        L"General",
        L"Appearance",
        L"Main UI",
        L"File Panel",
        L"About"
    };

    int y = 20;
    for (size_t i = 0; i < categories.size(); i++) {
        HWND btn = CreateWindowEx(0, L"BUTTON", categories[i].c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, y, NAV_WIDTH - 20, 35, m_hwnd, 
            (HMENU)(NAV_BUTTON_ID_BASE + i), m_hInstance, NULL);
        m_navButtons.push_back(btn);
        y += 45;
    }
}

void SettingsWindow::SwitchCategory(SettingCategory category) {
    m_currentCategory = category;
    ClearContentArea();
    DrawCategoryContent(category);
    InvalidateRect(m_hwnd, NULL, TRUE);
    UpdateWindow(m_hwnd);
}

void SettingsWindow::ClearContentArea() {
    for (HWND ctrl : m_contentControls) {
        DestroyWindow(ctrl);
    }
    m_contentControls.clear();
}

void SettingsWindow::DrawCategoryContent(SettingCategory category) {
    int x = NAV_WIDTH + 20;
    int y = 40;

    switch (category) {
        case SettingCategory::General:
        {
            HWND title = CreateWindowEx(0, L"STATIC", L"General Settings", WS_CHILD | WS_VISIBLE,
                x, y, 300, 28, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(title);
            y += 45;

            HWND darkLabel = CreateWindowEx(0, L"STATIC", L"Dark Mode:", WS_CHILD | WS_VISIBLE,
                x, y, 120, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(darkLabel);
            
            m_btnDarkMode = CreateWindowEx(0, L"BUTTON", m_darkMode ? L"ON" : L"OFF",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x + 140, y - 2, 90, 30, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 1), m_hInstance, NULL);
            m_contentControls.push_back(m_btnDarkMode);
            y += 50;

            HWND followLabel = CreateWindowEx(0, L"STATIC", L"Follow System Theme:", WS_CHILD | WS_VISIBLE,
                x, y, 160, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(followLabel);
            
            m_btnFollowSystem = CreateWindowEx(0, L"BUTTON", m_followSystemTheme ? L"ON" : L"OFF",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x + 180, y - 2, 90, 30, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 2), m_hInstance, NULL);
            m_contentControls.push_back(m_btnFollowSystem);
            y += 60;

            m_btnSave = CreateWindowEx(0, L"BUTTON", L"Save",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, 110, 35, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 10), m_hInstance, NULL);
            m_contentControls.push_back(m_btnSave);

            m_btnApply = CreateWindowEx(0, L"BUTTON", L"Save & Apply",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x + 125, y, 130, 35, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 11), m_hInstance, NULL);
            m_contentControls.push_back(m_btnApply);
            break;
        }

        case SettingCategory::Appearance:
        {
            HWND title = CreateWindowEx(0, L"STATIC", L"Appearance Settings", WS_CHILD | WS_VISIBLE,
                x, y, 300, 28, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(title);
            y += 45;

            HWND note = CreateWindowEx(0, L"STATIC", L"Theme settings are in General tab.", WS_CHILD | WS_VISIBLE,
                x, y, 350, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(note);
            break;
        }

        case SettingCategory::MainUI:
        {
            HWND title = CreateWindowEx(0, L"STATIC", L"Main UI Settings", WS_CHILD | WS_VISIBLE,
                x, y, 300, 28, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(title);
            y += 45;

            HWND widthLabel = CreateWindowEx(0, L"STATIC", L"Width:", WS_CHILD | WS_VISIBLE,
                x, y, 80, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(widthLabel);
            
            m_sliderMainWidth.hwnd = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x + 90, y + 4, 220, 28, m_hwnd, 
                (HMENU)(SLIDER_ID_BASE + 1), m_hInstance, NULL);
            SendMessage(m_sliderMainWidth.hwnd, TBM_SETRANGE, TRUE, MAKELONG(200, 600));
            SendMessage(m_sliderMainWidth.hwnd, TBM_SETPOS, TRUE, (int)m_mainUIWidth);
            m_contentControls.push_back(m_sliderMainWidth.hwnd);
            
            std::wstring widthVal = std::to_wstring((int)m_mainUIWidth);
            m_sliderMainWidth.valueHwnd = CreateWindowEx(0, L"STATIC", widthVal.c_str(),
                WS_CHILD | WS_VISIBLE, x + 325, y, 60, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(m_sliderMainWidth.valueHwnd);
            y += 48;

            HWND heightLabel = CreateWindowEx(0, L"STATIC", L"Height:", WS_CHILD | WS_VISIBLE,
                x, y, 80, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(heightLabel);
            
            m_sliderMainHeight.hwnd = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x + 90, y + 4, 220, 28, m_hwnd, 
                (HMENU)(SLIDER_ID_BASE + 2), m_hInstance, NULL);
            SendMessage(m_sliderMainHeight.hwnd, TBM_SETRANGE, TRUE, MAKELONG(100, 400));
            SendMessage(m_sliderMainHeight.hwnd, TBM_SETPOS, TRUE, (int)m_mainUIHeight);
            m_contentControls.push_back(m_sliderMainHeight.hwnd);
            
            std::wstring heightVal = std::to_wstring((int)m_mainUIHeight);
            m_sliderMainHeight.valueHwnd = CreateWindowEx(0, L"STATIC", heightVal.c_str(),
                WS_CHILD | WS_VISIBLE, x + 325, y, 60, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(m_sliderMainHeight.valueHwnd);
            y += 48;

            HWND transLabel = CreateWindowEx(0, L"STATIC", L"Transparency:", WS_CHILD | WS_VISIBLE,
                x, y, 80, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(transLabel);
            
            m_sliderMainTransparency.hwnd = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x + 90, y + 4, 220, 28, m_hwnd, 
                (HMENU)(SLIDER_ID_BASE + 3), m_hInstance, NULL);
            SendMessage(m_sliderMainTransparency.hwnd, TBM_SETRANGE, TRUE, MAKELONG(30, 100));
            SendMessage(m_sliderMainTransparency.hwnd, TBM_SETPOS, TRUE, (int)(m_mainUITransparency * 100));
            m_contentControls.push_back(m_sliderMainTransparency.hwnd);
            
            std::wstring transVal = std::to_wstring((int)(m_mainUITransparency * 100)) + L"%";
            m_sliderMainTransparency.valueHwnd = CreateWindowEx(0, L"STATIC", transVal.c_str(),
                WS_CHILD | WS_VISIBLE, x + 325, y, 70, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(m_sliderMainTransparency.valueHwnd);
            y += 60;

            m_btnSave = CreateWindowEx(0, L"BUTTON", L"Save",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, 110, 35, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 10), m_hInstance, NULL);
            m_contentControls.push_back(m_btnSave);

            m_btnApply = CreateWindowEx(0, L"BUTTON", L"Save & Apply",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x + 125, y, 130, 35, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 11), m_hInstance, NULL);
            m_contentControls.push_back(m_btnApply);
            break;
        }

        case SettingCategory::FilePanel:
        {
            HWND title = CreateWindowEx(0, L"STATIC", L"File Panel Settings", WS_CHILD | WS_VISIBLE,
                x, y, 300, 28, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(title);
            y += 45;

            HWND widthLabel = CreateWindowEx(0, L"STATIC", L"Width:", WS_CHILD | WS_VISIBLE,
                x, y, 80, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(widthLabel);
            
            m_sliderFileWidth.hwnd = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x + 90, y + 4, 220, 28, m_hwnd, 
                (HMENU)(SLIDER_ID_BASE + 4), m_hInstance, NULL);
            SendMessage(m_sliderFileWidth.hwnd, TBM_SETRANGE, TRUE, MAKELONG(200, 500));
            SendMessage(m_sliderFileWidth.hwnd, TBM_SETPOS, TRUE, (int)m_filePanelWidth);
            m_contentControls.push_back(m_sliderFileWidth.hwnd);
            
            std::wstring widthVal = std::to_wstring((int)m_filePanelWidth);
            m_sliderFileWidth.valueHwnd = CreateWindowEx(0, L"STATIC", widthVal.c_str(),
                WS_CHILD | WS_VISIBLE, x + 325, y, 60, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(m_sliderFileWidth.valueHwnd);
            y += 48;

            HWND heightLabel = CreateWindowEx(0, L"STATIC", L"Height:", WS_CHILD | WS_VISIBLE,
                x, y, 80, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(heightLabel);
            
            m_sliderFileHeight.hwnd = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x + 90, y + 4, 220, 28, m_hwnd, 
                (HMENU)(SLIDER_ID_BASE + 5), m_hInstance, NULL);
            SendMessage(m_sliderFileHeight.hwnd, TBM_SETRANGE, TRUE, MAKELONG(150, 400));
            SendMessage(m_sliderFileHeight.hwnd, TBM_SETPOS, TRUE, (int)m_filePanelHeight);
            m_contentControls.push_back(m_sliderFileHeight.hwnd);
            
            std::wstring heightVal = std::to_wstring((int)m_filePanelHeight);
            m_sliderFileHeight.valueHwnd = CreateWindowEx(0, L"STATIC", heightVal.c_str(),
                WS_CHILD | WS_VISIBLE, x + 325, y, 60, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(m_sliderFileHeight.valueHwnd);
            y += 48;

            HWND transLabel = CreateWindowEx(0, L"STATIC", L"Transparency:", WS_CHILD | WS_VISIBLE,
                x, y, 80, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(transLabel);
            
            m_sliderFileTransparency.hwnd = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x + 90, y + 4, 220, 28, m_hwnd, 
                (HMENU)(SLIDER_ID_BASE + 6), m_hInstance, NULL);
            SendMessage(m_sliderFileTransparency.hwnd, TBM_SETRANGE, TRUE, MAKELONG(30, 100));
            SendMessage(m_sliderFileTransparency.hwnd, TBM_SETPOS, TRUE, (int)(m_filePanelTransparency * 100));
            m_contentControls.push_back(m_sliderFileTransparency.hwnd);
            
            std::wstring transVal = std::to_wstring((int)(m_filePanelTransparency * 100)) + L"%";
            m_sliderFileTransparency.valueHwnd = CreateWindowEx(0, L"STATIC", transVal.c_str(),
                WS_CHILD | WS_VISIBLE, x + 325, y, 70, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(m_sliderFileTransparency.valueHwnd);
            y += 60;

            m_btnSave = CreateWindowEx(0, L"BUTTON", L"Save",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, 110, 35, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 10), m_hInstance, NULL);
            m_contentControls.push_back(m_btnSave);

            m_btnApply = CreateWindowEx(0, L"BUTTON", L"Save & Apply",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x + 125, y, 130, 35, m_hwnd, 
                (HMENU)(BUTTON_ID_BASE + 11), m_hInstance, NULL);
            m_contentControls.push_back(m_btnApply);
            break;
        }

        case SettingCategory::About:
        {
            HWND title = CreateWindowEx(0, L"STATIC", L"About Dynamic Island", WS_CHILD | WS_VISIBLE,
                x, y, 300, 28, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(title);
            y += 45;

            HWND version = CreateWindowEx(0, L"STATIC", L"Version 1.2.0", WS_CHILD | WS_VISIBLE,
                x, y, 300, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(version);
            y += 35;

            HWND desc = CreateWindowEx(0, L"STATIC", L"A Dynamic Island implementation for Windows", WS_CHILD | WS_VISIBLE,
                x, y, 380, 24, m_hwnd, NULL, m_hInstance, NULL);
            m_contentControls.push_back(desc);
            break;
        }
    }
}

void SettingsWindow::Show() {
    if (m_hwnd) { 
        ShowWindow(m_hwnd, SW_SHOW); 
        m_visible = true; 
        SwitchCategory(m_currentCategory);
    }
}

void SettingsWindow::Hide() {
    if (m_hwnd) { 
        ShowWindow(m_hwnd, SW_HIDE); 
        m_visible = false; 
    }
}

void SettingsWindow::Toggle() {
    if (m_visible) Hide(); else Show();
}

LRESULT SettingsWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, m_darkMode ? RGB(220, 220, 220) : RGB(50, 50, 50));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }

        case WM_CTLCOLORBTN:
        {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, m_darkMode ? RGB(220, 220, 220) : RGB(50, 50, 50));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id >= NAV_BUTTON_ID_BASE && id < NAV_BUTTON_ID_BASE + 5) {
                SwitchCategory((SettingCategory)(id - NAV_BUTTON_ID_BASE));
                return 0;
            }

            if (id == BUTTON_ID_BASE + 1 && code == BN_CLICKED) {
                m_darkMode = !m_darkMode;
                SetWindowText(m_btnDarkMode, m_darkMode ? L"ON" : L"OFF");
                m_followSystemTheme = false;
                SetWindowText(m_btnFollowSystem, L"OFF");
                ApplyTheme();
                return 0;
            }

            if (id == BUTTON_ID_BASE + 2 && code == BN_CLICKED) {
                m_followSystemTheme = !m_followSystemTheme;
                SetWindowText(m_btnFollowSystem, m_followSystemTheme ? L"ON" : L"OFF");
                if (m_followSystemTheme) {
                    m_darkMode = GetSystemDarkMode();
                    SetWindowText(m_btnDarkMode, m_darkMode ? L"ON" : L"OFF");
                    ApplyTheme();
                }
                return 0;
            }

            if (id == BUTTON_ID_BASE + 10 && code == BN_CLICKED) {
                m_mainUIWidth = (float)SendMessage(m_sliderMainWidth.hwnd, TBM_GETPOS, 0, 0);
                m_mainUIHeight = (float)SendMessage(m_sliderMainHeight.hwnd, TBM_GETPOS, 0, 0);
                m_mainUITransparency = (float)SendMessage(m_sliderMainTransparency.hwnd, TBM_GETPOS, 0, 0) / 100.0f;
                m_filePanelWidth = (float)SendMessage(m_sliderFileWidth.hwnd, TBM_GETPOS, 0, 0);
                m_filePanelHeight = (float)SendMessage(m_sliderFileHeight.hwnd, TBM_GETPOS, 0, 0);
                m_filePanelTransparency = (float)SendMessage(m_sliderFileTransparency.hwnd, TBM_GETPOS, 0, 0) / 100.0f;
                SaveSettings();
                MessageBox(hwnd, L"Settings saved!", L"Success", MB_OK);
                return 0;
            }

            if (id == BUTTON_ID_BASE + 11 && code == BN_CLICKED) {
                m_mainUIWidth = (float)SendMessage(m_sliderMainWidth.hwnd, TBM_GETPOS, 0, 0);
                m_mainUIHeight = (float)SendMessage(m_sliderMainHeight.hwnd, TBM_GETPOS, 0, 0);
                m_mainUITransparency = (float)SendMessage(m_sliderMainTransparency.hwnd, TBM_GETPOS, 0, 0) / 100.0f;
                m_filePanelWidth = (float)SendMessage(m_sliderFileWidth.hwnd, TBM_GETPOS, 0, 0);
                m_filePanelHeight = (float)SendMessage(m_sliderFileHeight.hwnd, TBM_GETPOS, 0, 0);
                m_filePanelTransparency = (float)SendMessage(m_sliderFileTransparency.hwnd, TBM_GETPOS, 0, 0) / 100.0f;
                ApplySettings();
                MessageBox(hwnd, L"Settings saved and applied!", L"Success", MB_OK);
                return 0;
            }
            break;
        }

        case WM_HSCROLL:
        {
            HWND sliderHwnd = (HWND)lParam;
            int pos = SendMessage(sliderHwnd, TBM_GETPOS, 0, 0);

            if (sliderHwnd == m_sliderMainWidth.hwnd) {
                std::wstring valStr = std::to_wstring(pos);
                SetWindowText(m_sliderMainWidth.valueHwnd, valStr.c_str());
                m_mainUIWidth = (float)pos;
            } else if (sliderHwnd == m_sliderMainHeight.hwnd) {
                std::wstring valStr = std::to_wstring(pos);
                SetWindowText(m_sliderMainHeight.valueHwnd, valStr.c_str());
                m_mainUIHeight = (float)pos;
            } else if (sliderHwnd == m_sliderMainTransparency.hwnd) {
                std::wstring valStr = std::to_wstring(pos) + L"%";
                SetWindowText(m_sliderMainTransparency.valueHwnd, valStr.c_str());
                m_mainUITransparency = (float)pos / 100.0f;
            } else if (sliderHwnd == m_sliderFileWidth.hwnd) {
                std::wstring valStr = std::to_wstring(pos);
                SetWindowText(m_sliderFileWidth.valueHwnd, valStr.c_str());
                m_filePanelWidth = (float)pos;
            } else if (sliderHwnd == m_sliderFileHeight.hwnd) {
                std::wstring valStr = std::to_wstring(pos);
                SetWindowText(m_sliderFileHeight.valueHwnd, valStr.c_str());
                m_filePanelHeight = (float)pos;
            } else if (sliderHwnd == m_sliderFileTransparency.hwnd) {
                std::wstring valStr = std::to_wstring(pos) + L"%";
                SetWindowText(m_sliderFileTransparency.valueHwnd, valStr.c_str());
                m_filePanelTransparency = (float)pos / 100.0f;
            }
            return 0;
        }

        case WM_SETTINGCHANGE:
        {
            if (m_followSystemTheme) {
                m_darkMode = GetSystemDarkMode();
                if (m_btnDarkMode) {
                    SetWindowText(m_btnDarkMode, m_darkMode ? L"ON" : L"OFF");
                }
                ApplyTheme();
            }
            return 0;
        }

        case WM_CLOSE:
            Hide();
            return 0;

        case WM_DESTROY:
            m_hwnd = NULL;
            m_visible = false;
            break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


