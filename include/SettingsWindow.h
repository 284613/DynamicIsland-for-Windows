#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <vector>
#include "IslandState.h"
#include "Spring.h"
#include "settings/SettingsControls.h"

enum class SettingCategory {
    General,
    Appearance,
    MainUI,
    FilePanel,
    Weather,
    Notifications,
    FaceUnlock,
    Agent,
    Advanced,
    About
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

    bool CreateDeviceIndependentResources();
    bool EnsureDeviceResources();
    void DiscardDeviceResources();
    void ResizeRenderTarget(UINT width, UINT height);
    void ApplyRoundedRegion() const;
    void StartAnimationTimer();
    void StopAnimationTimerIfIdle();
    bool TickAnimations(float deltaTime);
    bool HasActiveAnimations() const;
    bool UpdateControlAnimations(std::vector<SettingsControl>& controls, float deltaTime);

    void SwitchCategory(SettingCategory category);
    void BuildPage(SettingCategory category, std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildGeneralPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildAppearancePage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildMainUiPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildFilePanelPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildWeatherPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildNotificationsPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildFaceUnlockPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildAgentPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildAdvancedPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void BuildAboutPage(std::vector<SettingsControl>& controls, float& contentHeight) const;
    void RebuildFooterControls();
    void BuildCompactModesCard(std::vector<SettingsControl>& controls, float y, float& cardHeight) const;
    void BuildMusicArtworkCard(std::vector<SettingsControl>& controls, float y, float& cardHeight) const;
    void BuildMusicLyricsCard(std::vector<SettingsControl>& controls, float y, float& cardHeight) const;
    std::wstring SerializeCompactModeOrder() const;
    void LoadCompactModeOrder(const std::wstring& rawValue);
    bool IsCompactModeEnabled(const std::wstring& modeKey) const;
    void ToggleCompactMode(const std::wstring& modeKey);
    void MoveCompactMode(const std::wstring& modeKey, int delta);
    std::wstring GetFaceUnlockStatusText() const;
    size_t GetFaceTemplateCount() const;

    void Render();
    void DrawWindowBackground();
    void DrawTitleBar();
    void DrawSidebar();
    void DrawNavItem(int idx, const wchar_t* icon, const wchar_t* label, float y, bool active, float hoverAlpha);
    void DrawHeader();
    void DrawFooter();
    void DrawPageLayer(const std::vector<SettingsControl>& controls, float alpha, float offsetY, float scrollY);
    void DrawControl(const SettingsControl& control, float alpha, float offsetY, float scrollY);
    void DrawTextBlock(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        const D2D1_COLOR_F& color, float alpha, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE);
    void DrawTextBlock(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        const D2D1_COLOR_F& color, float alpha, DWRITE_TEXT_ALIGNMENT horizontalAlignment, DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment);
    void DrawToggleControl(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void DrawSliderControl(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void DrawTextInputCtrl(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void DrawCitySelectorCtrl(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void DrawButtonAtRect(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void DrawSeparatorLine(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void DrawLabelRow(const SettingsControl& control, const D2D1_RECT_F& rect, float alpha);
    void FillRoundedRect(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color, float alpha = 1.0f);
    void DrawRoundedRect(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color, float strokeWidth = 1.0f, float alpha = 1.0f);
    void FillEllipse(const D2D1_ELLIPSE& ellipse, const D2D1_COLOR_F& color, float alpha = 1.0f);
    void DrawFocusRing(const D2D1_RECT_F& rect, float radius, float alpha = 1.0f);
    D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha) const;

    D2D1_RECT_F GetWindowRectDip() const;
    D2D1_RECT_F GetSidebarRect() const;
    D2D1_RECT_F GetHeaderRect() const;
    D2D1_RECT_F GetContentViewportRect() const;
    D2D1_RECT_F GetFooterRect() const;
    D2D1_RECT_F GetNavigationItemRect(size_t index) const;
    D2D1_RECT_F GetTrafficLightRect(int index) const;
    D2D1_RECT_F GetCloseButtonRect() const;
    float GetTargetScroll() const;
    float ClampScroll(float value) const;

    void UpdateTheme();
    void ApplyTheme();
    bool GetSystemDarkMode();
    void ResetToDefaults();
    void RefreshControlTargets();
    void RefreshScrollBounds(bool snap);
    void MarkDirty(bool dirty = true, const std::wstring& statusText = L"");
    void SetStatusText(const std::wstring& statusText);
    void CenterToParent() const;
    void HideImmediately();
    std::wstring ResolveConfigPath() const;
    std::wstring GetSettingsPath() const;
    std::wstring GetConfigPath() const;

    SettingsControl* FindControlById(std::vector<SettingsControl>& controls, int id);
    const SettingsControl* FindControlById(const std::vector<SettingsControl>& controls, int id) const;
    SettingsControl* HitTestInteractiveControl(float x, float y);
    const SettingsControl* HitTestInteractiveControl(float x, float y) const;
    int HitTestNavigation(float x, float y) const;
    int HitTestTrafficLight(float x, float y) const;
    bool UpdateHoverState(float x, float y);
    void BeginSliderDrag(int controlId, float x);
    void UpdateSliderDrag(float x);
    void EndSliderDrag();
    void HandleControlActivation(int controlId);
    void SetFocusedControl(int controlId);
    void FocusNextControl(bool reverse);
    void UpdateBoundTextValue(int controlId, const std::wstring& value);
    void SyncStateFromControl(int controlId);
    void SyncControlFromState(SettingsControl& control) const;
    bool IsControlFocusable(const SettingsControl& control) const;
    float PixelsToDipX(int x) const;
    float PixelsToDipY(int y) const;
    int DipToPixels(float value) const;

    struct CityInfo {
        std::wstring id;
        std::wstring nameZH;
        std::wstring nameEN;
        std::wstring adm1;
        std::wstring adm2;
    };

    struct LocationResult {
        std::wstring name;
        std::wstring id;
        bool success = false;
    };

    void LoadCities();
    void UpdateCityFilter(const std::wstring& query);
    void AutoLocateCity();
    void PositionIMEWindow();
    D2D1_RECT_F GetRegionPillRect(const D2D1_RECT_F& inputBoxRect) const;
    D2D1_RECT_F GetCitySearchTextRect(const D2D1_RECT_F& inputBoxRect) const;

    HWND m_hwnd = nullptr;

    HWND m_parentHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    bool m_visible = false;
    bool m_isDirty = false;
    bool m_hidePending = false;
    bool m_mouseTracking = false;
    SettingCategory m_currentCategory = SettingCategory::General;
    std::wstring m_statusText;
    std::wstring m_configPath;

    Microsoft::WRL::ComPtr<ID2D1Factory1> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_sectionFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_bodyFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_captionFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_iconFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_solidBrush;

    std::vector<SettingsControl> m_activeControls;
    std::vector<SettingsControl> m_outgoingControls;
    std::vector<SettingsControl> m_footerControls;
    float m_activeContentHeight = 0.0f;
    float m_outgoingContentHeight = 0.0f;

    std::array<Spring, 10> m_navHoverSprings;
    std::array<Spring, 3> m_trafficLightHoverSprings;
    Spring m_sidebarSelectionY = SpringFactory::CreateDefault();
    Spring m_contentInAlpha = SpringFactory::CreateDefault();
    Spring m_contentOutAlpha = SpringFactory::CreateDefault();
    Spring m_contentInOffsetY = SpringFactory::CreateDefault();
    Spring m_contentOutOffsetY = SpringFactory::CreateDefault();
    Spring m_scrollSpring = SpringFactory::CreateSmooth();
    Spring m_windowAlpha = SpringFactory::CreateSmooth();
    Spring m_statusAlpha = SpringFactory::CreateSmooth();
    Spring m_closeButtonHover = SpringFactory::CreateSmooth();

    ULONGLONG m_lastAnimationTick = 0;
    ULONGLONG m_caretBlinkStart = 0;
    bool m_caretVisible = true;
    int m_hoveredNavIndex = -1;
    int m_pressedNavIndex = -1;
    int m_hoveredTrafficLight = -1;
    int m_pressedTrafficLight = -1;
    bool m_hoveredCloseButton = false;
    int m_hoveredControlId = 0;
    int m_pressedControlId = 0;
    int m_focusedControlId = 0;
    int m_activeTextInputId = 0;
    int m_draggingSliderId = 0;

    D2D1_COLOR_F m_windowColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_sidebarColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_cardColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_cardStrokeColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_textColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_secondaryTextColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_hairlineColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_sidebarSelectionColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_footerColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_inputFillColor = D2D1::ColorF(0.f, 0.f, 0.f, 1.f);
    D2D1_COLOR_F m_accentColor = D2D1::ColorF(0.30f, 0.49f, 1.0f, 1.0f);

    bool m_darkMode = false;
    bool m_followSystemTheme = true;
    float m_mainUIWidth = 400.0f;
    float m_mainUIHeight = 420.0f;
    float m_mainUITransparency = 1.0f;
    float m_filePanelWidth = 340.0f;
    float m_filePanelHeight = 200.0f;
    float m_filePanelTransparency = 0.9f;
    int m_themeAccentIndex = 0;
    int m_fontScale = 100;
    std::wstring m_weatherCity = L"北京";
    std::wstring m_weatherApiKey;
    std::wstring m_weatherLocationId = L"101010100";
    std::wstring m_allowedApps = L"微信,QQ";
    bool m_autoStart = false;
    bool m_facePerformanceMode = false;
    float m_springStiffness = 100.0f;
    float m_springDamping = 10.0f;
    float m_lowBatteryThreshold = 20.0f;
    float m_fileStashMaxItems = 5.0f;
    float m_mediaPollIntervalMs = 1000.0f;
    float m_dpiScale = 1.0f;
    std::vector<std::wstring> m_compactModeOrder;
    bool m_compactAlbumVinyl = true;
    bool m_expandedAlbumVinyl = false;
    LyricTranslationMode m_lyricTranslationMode = LyricTranslationMode::CurrentOnly;
    bool m_vinylRingPulse = true;

    std::vector<CityInfo> m_allCities;
    std::vector<const CityInfo*> m_filteredCities;
    std::vector<std::wstring> m_allRegions;  // 从城市数据中提取的去重省份列表
    bool m_citySearchActive = false;
    int m_selectedCityIndex = -1;
    bool m_isLocating = false;
    std::wstring m_citySearchText;    // 搜索框当前输入，跨页面重建保持不变
    std::wstring m_selectedRegion;    // 当前筛选省份 (adm1)，空字符串表示全部
    std::wstring m_selectedPrefecture; // 当前筛选地级市 (adm2)，空字符串表示全部
    bool m_regionPickerOpen = false;  // 省份/地级市选择面板是否展开
    int  m_regionPickerLevel = 0;     // 0 = 省份列表，1 = 该省的地级市列表
};

