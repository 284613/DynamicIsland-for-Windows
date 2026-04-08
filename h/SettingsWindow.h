#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

enum class SettingCategory {
    General,
    Appearance,
    MainUI,
    FilePanel,
    About
};

struct SliderControl {
    HWND hwnd;
    HWND labelHwnd;
    HWND valueHwnd;
    float minValue;
    float maxValue;
    float currentValue;
    std::wstring label;
};

class SettingsWindow {
public:
    SettingsWindow();
    ~SettingsWindow();
    bool Create(HINSTANCE hInstance, HWND parentHwnd);
    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const { return m_visible; }
    void LoadSettings();
    void SaveSettings();
    void ApplySettings();

    bool IsDarkMode() const { return m_darkMode; }
    bool IsFollowSystemTheme() const { return m_followSystemTheme; }
    float GetMainUIWidth() const { return m_mainUIWidth; }
    float GetMainUIHeight() const { return m_mainUIHeight; }
    float GetMainUITransparency() const { return m_mainUITransparency; }
    float GetFilePanelWidth() const { return m_filePanelWidth; }
    float GetFilePanelHeight() const { return m_filePanelHeight; }
    float GetFilePanelTransparency() const { return m_filePanelTransparency; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void CreateNavigationButtons();
    void SwitchCategory(SettingCategory category);
    void DrawCategoryContent(SettingCategory category);
    void ClearContentArea();
    void UpdateSliderValue(SliderControl& slider, int pos);
    void ApplyTheme();
    bool GetSystemDarkMode();

    HWND m_hwnd = NULL;
    HWND m_parentHwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    bool m_visible = false;
    SettingCategory m_currentCategory = SettingCategory::General;

    HWND m_navPanel = NULL;
    HWND m_contentPanel = NULL;
    std::vector<HWND> m_navButtons;
    std::vector<HWND> m_contentControls;

    bool m_darkMode = false;
    bool m_followSystemTheme = true;
    float m_mainUIWidth = 400.0f;
    float m_mainUIHeight = 200.0f;
    float m_mainUITransparency = 1.0f;
    float m_filePanelWidth = 340.0f;
    float m_filePanelHeight = 240.0f;
    float m_filePanelTransparency = 0.9f;

    SliderControl m_sliderMainWidth;
    SliderControl m_sliderMainHeight;
    SliderControl m_sliderMainTransparency;
    SliderControl m_sliderFileWidth;
    SliderControl m_sliderFileHeight;
    SliderControl m_sliderFileTransparency;

    HWND m_btnDarkMode = NULL;
    HWND m_btnFollowSystem = NULL;
    HWND m_btnSave = NULL;
    HWND m_btnApply = NULL;

    static constexpr int WINDOW_WIDTH = 600;
    static constexpr int WINDOW_HEIGHT = 450;
    static constexpr int NAV_WIDTH = 150;
};


