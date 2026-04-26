// ============================================================
// SettingsWindow.cpp  --  仿 macOS System Settings
// Direct2D 全自绘，弹簧动画，8 个设置类别
// ============================================================
#include "SettingsWindow.h"
#include "Messages.h"
#include "ClaudeHookInstaller.h"
#include "ClaudeHookBridge.h"
#include "CodexHookInstaller.h"
#include "CodexHookBridge.h"
#include "AutoStartManager.h"
#include "FaceEnrollWindow.h"
#include "face_core/FaceTemplateStore.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <shlobj.h>
#include <sstream>
#include <thread>
#include <winhttp.h>
#include <imm.h>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "imm32.lib")

// ---- 颜色工具 -------------------------------------------------------
static D2D1_COLOR_F CF(float r, float g, float b, float a = 1.f) {
    return D2D1::ColorF(r, g, b, a);
}
static D2D1_COLOR_F LerpCF(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return CF(a.r + (b.r - a.r) * t,
              a.g + (b.g - a.g) * t,
              a.b + (b.b - a.b) * t,
              a.a + (b.a - a.a) * t);
}

// ---- 类别元数据 -------------------------------------------------------
struct CatMeta { const wchar_t* icon; const wchar_t* label; const wchar_t* title; const wchar_t* subtitle; };
static const CatMeta kCats[] = {
    { L"\uE713", L"通用",     L"通用",     L"主题、行为与整体偏好" },
    { L"\uE790", L"外观",     L"外观",     L"统一视觉语言与桌面风格" },
    { L"\uE737", L"主岛",     L"主岛",     L"调整主岛尺寸与透明度" },
    { L"\uE8B7", L"文件副岛", L"文件副岛", L"微调文件寄存面板的尺寸与通透感" },
    { L"\uE9CA", L"天气",     L"天气",     L"和风天气 API 与城市配置" },
    { L"\uEA8F", L"通知",     L"通知",     L"允许接管通知的应用列表" },
    { L"\uE8D7", L"人脸解锁", L"人脸解锁", L"录入、模板管理与桌面端反馈" },
    { L"\uE71D", L"Agent",    L"Agent Hooks",  L"安装 Claude / Codex hooks，并检测运行态监听状态" },
    { L"\uE90F", L"高级",     L"高级",     L"物理参数与系统行为调优" },
    { L"\uE946", L"关于",     L"关于",     L"项目版本与设计说明" },
};
static constexpr int kCatCount = 10;

// ---- 常量（DIP 基准） -----------------------------------------------
namespace {
    const wchar_t CLASS_NAME[]   = L"DI_SettingsWindowV2";
    const wchar_t CONFIG_FILE[]  = L"config.ini";
    constexpr UINT_PTR ANIM_TIMER = 41;
    constexpr float ROW_H = 54.f;    // 普通行高
    constexpr float SLIDER_H = 78.f; // 滑块行高
    constexpr float INPUT_H  = 82.f; // 文本输入行高
    constexpr float CARD_PAD = 12.f; // 卡片内上下 padding
    constexpr float CARD_GAP = 14.f; // 卡片间距
    constexpr float CONT_PAD = 20.f; // 内容区左右 padding
    // 导航
    constexpr float NAV_X    = 12.f;
    constexpr float NAV_W    = 196.f;
    constexpr float NAV_H    = 40.f;
    constexpr float NAV_GAP  = 5.f;
    constexpr float NAV_Y0   = 72.f;
    // 窗口布局
    constexpr float WIN_W    = 960.f;
    constexpr float WIN_H    = 650.f;
    constexpr float SIDEBAR  = 220.f;
    constexpr float TITLEBAR = 52.f;
    constexpr float HEADER   = 92.f;
    constexpr float FOOTER   = 78.f;
    constexpr float CONT_L   = SIDEBAR + CONT_PAD;
    constexpr float CONT_T   = TITLEBAR + HEADER;
    constexpr float CONT_B   = WIN_H - FOOTER;
    constexpr float CONT_W   = WIN_W - CONT_L - CONT_PAD;
    constexpr float CONT_VH  = CONT_B - CONT_T;   // 可见内容高度
    // 滑块轨道
    constexpr float TRACK_W  = 320.f;
    constexpr float TRACK_H  = 4.f;
    constexpr float KNOB_R   = 9.f;
    constexpr float TRAFFIC_LIGHT_START_X = 18.f;
    constexpr float TRAFFIC_LIGHT_SPACING = 18.f;
    constexpr float TRAFFIC_LIGHT_RADIUS = 6.f;
    constexpr float WINDOW_CORNER_RADIUS = 24.f;

    struct CompactModeMeta {
        const wchar_t* key;
        const wchar_t* label;
        const wchar_t* subtitle;
        int toggleId;
        int upId;
        int downId;
    };

    const CompactModeMeta kCompactModes[] = {
        { L"Music",    L"Music",    L"音乐紧凑态",     SettingID::COMPACT_MUSIC_TOGGLE, SettingID::COMPACT_MUSIC_UP, SettingID::COMPACT_MUSIC_DOWN },
        { L"Pomodoro", L"Pomodoro", L"番茄钟紧凑态",   SettingID::COMPACT_POMO_TOGGLE,  SettingID::COMPACT_POMO_UP,  SettingID::COMPACT_POMO_DOWN },
        { L"Todo",     L"Todo",     L"TODO 列表紧凑态", SettingID::COMPACT_TODO_TOGGLE, SettingID::COMPACT_TODO_UP,  SettingID::COMPACT_TODO_DOWN },
        { L"Agent",    L"Agent",    L"Agent 会话紧凑态", SettingID::COMPACT_AGENT_TOGGLE, SettingID::COMPACT_AGENT_UP, SettingID::COMPACT_AGENT_DOWN },
    };

    std::wstring Utf8ToWide(const std::string& utf8) {
        if (utf8.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
        if (len <= 0) return {};
        std::wstring wide(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), wide.data(), len);
        return wide;
    }

    std::string WideToUtf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), utf8.data(), len, nullptr, nullptr);
        return utf8;
    }

    std::wstring TrimLineEndings(std::wstring line) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
            line.pop_back();
        }
        return line;
    }

    bool StartsWithKey(const std::wstring& line, const std::wstring& key) {
        return line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == L'=';
    }

    std::wstring ReadUtf8IniValue(const std::wstring& path, const std::wstring& section, const std::wstring& key, const std::wstring& fallback) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return fallback;
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::wstring text = Utf8ToWide(bytes);
        if (text.empty()) return fallback;

        std::wistringstream stream(text);
        std::wstring line;
        std::wstring currentSection;
        const std::wstring targetSection = L"[" + section + L"]";
        while (std::getline(stream, line)) {
            line = TrimLineEndings(line);
            if (line.empty()) continue;
            if (line.front() == L'[' && line.back() == L']') {
                currentSection = line;
                continue;
            }
            if (currentSection == targetSection && StartsWithKey(line, key)) {
                return line.substr(key.size() + 1);
            }
        }
        return fallback;
    }

    bool IsVinylStyle(const std::wstring& value, bool fallback) {
        if (CompareStringOrdinal(value.c_str(), -1, L"Vinyl", -1, TRUE) == CSTR_EQUAL) {
            return true;
        }
        if (CompareStringOrdinal(value.c_str(), -1, L"Square", -1, TRUE) == CSTR_EQUAL) {
            return false;
        }
        return fallback;
    }

    void WriteUtf8IniValue(const std::wstring& path, const std::wstring& section, const std::wstring& key, const std::wstring& value) {
        std::ifstream in(path, std::ios::binary);
        std::string bytes;
        if (in.is_open()) {
            bytes.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        std::wstring text = bytes.empty() ? L"" : Utf8ToWide(bytes);
        std::vector<std::wstring> lines;
        {
            std::wistringstream stream(text);
            std::wstring line;
            while (std::getline(stream, line)) {
                lines.push_back(TrimLineEndings(line));
            }
        }

        const std::wstring targetSection = L"[" + section + L"]";
        const std::wstring keyLine = key + L"=" + value;
        bool sectionFound = false;
        bool keyWritten = false;
        size_t insertPos = lines.size();

        for (size_t i = 0; i < lines.size(); ++i) {
            const std::wstring& line = lines[i];
            if (line == targetSection) {
                sectionFound = true;
                insertPos = i + 1;
                continue;
            }
            if (sectionFound && !line.empty() && line.front() == L'[' && line.back() == L']') {
                insertPos = i;
                break;
            }
            if (sectionFound && StartsWithKey(line, key)) {
                lines[i] = keyLine;
                keyWritten = true;
                break;
            }
        }

        if (!keyWritten) {
            if (!sectionFound) {
                if (!lines.empty() && !lines.back().empty()) {
                    lines.push_back(L"");
                }
                lines.push_back(targetSection);
                lines.push_back(keyLine);
            } else {
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertPos), keyLine);
            }
        }

        std::wstring output;
        for (size_t i = 0; i < lines.size(); ++i) {
            output += lines[i];
            output += L"\r\n";
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::string utf8 = WideToUtf8(output);
        out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }

    std::wstring ReadClipboardUnicodeText(HWND hwnd) {
        if (!OpenClipboard(hwnd)) {
            return {};
        }

        std::wstring text;
        HANDLE clipboardData = GetClipboardData(CF_UNICODETEXT);
        if (clipboardData) {
            const wchar_t* raw = static_cast<const wchar_t*>(GlobalLock(clipboardData));
            if (raw) {
                text = raw;
                GlobalUnlock(clipboardData);
            }
        }

        CloseClipboard();
        return text;
    }

    const CompactModeMeta* FindCompactModeMetaById(int id) {
        for (const auto& mode : kCompactModes) {
            if (mode.toggleId == id || mode.upId == id || mode.downId == id) {
                return &mode;
            }
        }
        return nullptr;
    }

    std::wstring TrimWeatherAreaSuffix(std::wstring text) {
        if (text.size() > 1) {
            const wchar_t tail = text.back();
            if (tail == L'市' || tail == L'区' || tail == L'县') {
                text.pop_back();
            }
        }
        return text;
    }

}

// ====================================================================
// 构造 / 析构
// ====================================================================
SettingsWindow::SettingsWindow() {
    m_contentInAlpha.SetTarget(1.f);
    m_contentInAlpha.SnapToTarget();
    m_contentOutAlpha.SetTarget(0.f);
    m_contentOutAlpha.SnapToTarget();
    m_windowAlpha.SetTarget(0.f);
    m_windowAlpha.SnapToTarget();
    m_scrollSpring.SetStiffness(120.f);
    m_scrollSpring.SetDamping(20.f);
    m_sidebarSelectionY.SetStiffness(220.f);
    m_sidebarSelectionY.SetDamping(22.f);
    m_contentInAlpha.SetStiffness(160.f);
    m_contentInAlpha.SetDamping(22.f);
    m_contentOutAlpha.SetStiffness(160.f);
    m_contentOutAlpha.SetDamping(22.f);
    m_contentInOffsetY.SetStiffness(160.f);
    m_contentInOffsetY.SetDamping(22.f);
    m_contentOutOffsetY.SetStiffness(160.f);
    m_contentOutOffsetY.SetDamping(22.f);
    m_windowAlpha.SetStiffness(180.f);
    m_windowAlpha.SetDamping(24.f);
    m_statusAlpha.SetStiffness(120.f);
    m_statusAlpha.SetDamping(20.f);
    m_closeButtonHover.SetStiffness(240.f);
    m_closeButtonHover.SetDamping(26.f);
    for (auto& s : m_navHoverSprings) {
        s.SetStiffness(200.f); s.SetDamping(24.f);
    }
    for (auto& s : m_trafficLightHoverSprings) {
        s.SetStiffness(220.f); s.SetDamping(24.f);
    }
    LoadCities();
}

void SettingsWindow::LoadCities() {
    std::wstring exePath(MAX_PATH, L'\0');
    GetModuleFileNameW(nullptr, &exePath[0], MAX_PATH);
    size_t pos = exePath.find_last_of(L"\\/");
    std::wstring cityFile = (pos != std::wstring::npos ? exePath.substr(0, pos + 1) : L"") + L"resources\\cities.json";

    HANDLE hFile = CreateFileW(cityFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        // 尝试上一级目录 resources
        cityFile = (pos != std::wstring::npos ? exePath.substr(0, pos + 1) : L"") + L"..\\resources\\cities.json";
        hFile = CreateFileW(cityFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0) { CloseHandle(hFile); return; }

    std::string utf8Data(fileSize, '\0');
    DWORD readSize = 0;
    ReadFile(hFile, &utf8Data[0], fileSize, &readSize, nullptr);
    CloseHandle(hFile);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Data.c_str(), -1, nullptr, 0);
    std::wstring wdata(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Data.c_str(), -1, &wdata[0], wlen);

    auto extract = [](const std::wstring& obj, const wchar_t* key) -> std::wstring {
        std::wstring pattern = L"\"";
        pattern += key;
        pattern += L"\":";
        size_t p = obj.find(pattern);
        if (p == std::wstring::npos) return L"";
        p += pattern.length();
        while (p < obj.length() && (obj[p] == L' ' || obj[p] == L'\t' || obj[p] == L'\"' || obj[p] == L':')) p++;
        size_t end = p;
        while (end < obj.length() && obj[end] != L'\"' && obj[end] != L',' && obj[end] != L'}') end++;
        
        std::wstring val = obj.substr(p, end - p);
        // 去除首尾可能的引号和空格
        if (!val.empty() && val.front() == L'\"') val.erase(0, 1);
        if (!val.empty() && val.back() == L'\"') val.pop_back();
        return val;
    };

    size_t offset = 0;
    while (true) {
        size_t start = wdata.find(L'{', offset);
        if (start == std::wstring::npos) break;
        size_t end = wdata.find(L'}', start);
        if (end == std::wstring::npos) break;
        std::wstring obj = wdata.substr(start, end - start + 1);
        CityInfo info;
        info.id = extract(obj, L"Location_ID");
        info.nameZH = extract(obj, L"Location_Name_ZH");
        info.nameEN = extract(obj, L"Location_Name_EN");
        info.adm1 = extract(obj, L"Adm1_Name_ZH");
        info.adm2 = extract(obj, L"Adm2_Name_ZH");
        if (!info.id.empty()) {
            m_allCities.push_back(info);
        }
        offset = end + 1;
    }

    // 提取去重省份列表，保持首次出现的顺序
    std::vector<std::wstring> seen;
    for (const auto& c : m_allCities) {
        const std::wstring& r = c.adm1;
        if (!r.empty() && std::find(seen.begin(), seen.end(), r) == seen.end()) {
            seen.push_back(r);
        }
    }
    m_allRegions = std::move(seen);
}

void SettingsWindow::UpdateCityFilter(const std::wstring& query) {
    m_filteredCities.clear();
    m_selectedCityIndex = -1;
    m_citySearchActive = true;

    // 三个条件都为空时不展示候选（避免初始状态就弹出）
    if (query.empty() && m_selectedRegion.empty() && m_selectedPrefecture.empty()) return;

    std::wstring q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (const auto& city : m_allCities) {
        if (!m_selectedRegion.empty()    && city.adm1 != m_selectedRegion)    continue;
        if (!m_selectedPrefecture.empty() && city.adm2 != m_selectedPrefecture) continue;

        if (!q.empty()) {
            std::wstring zh = city.nameZH;
            std::wstring en = city.nameEN;
            std::transform(en.begin(), en.end(), en.begin(), ::tolower);
            if (zh.find(q) == std::wstring::npos && en.find(q) != 0) continue;
        }

        m_filteredCities.push_back(&city);
        if (m_filteredCities.size() >= 8) break;
    }
}

SettingsWindow::~SettingsWindow() {
    DiscardDeviceResources();
    if (m_hwnd) DestroyWindow(m_hwnd);
}

// ====================================================================
// Create
// ====================================================================
bool SettingsWindow::Create(HINSTANCE hInstance, HWND parentHwnd) {
    if (m_hwnd) return true;
    m_hInstance  = hInstance;
    m_parentHwnd = parentHwnd;
    m_configPath = ResolveConfigPath();
    UINT dpi = parentHwnd ? GetDpiForWindow(parentHwnd) : GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    m_dpiScale = float(dpi) / 96.0f;
    int physicalWidth = DipToPixels(WIN_W);
    int physicalHeight = DipToPixels(WIN_H);

    if (!CreateDeviceIndependentResources()) return false;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        CLASS_NAME, L"Dynamic Island 设置",
        WS_POPUP | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, physicalWidth, physicalHeight,
        parentHwnd, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    // 圆角窗口
    ApplyRoundedRegion();
    const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);

    if (!EnsureDeviceResources()) return false;

    LoadSettings();
    UpdateTheme();
    // 初始化选中指示器位置
    m_sidebarSelectionY.SetTarget(NAV_Y0);
    m_sidebarSelectionY.SnapToTarget();
    SwitchCategory(SettingCategory::General);
    return true;
}

// ====================================================================
// 设备无关资源 (DWrite + D2D factory)
// ====================================================================
bool SettingsWindow::CreateDeviceIndependentResources() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    auto mkfmt = [&](const wchar_t* font, float size, DWRITE_FONT_WEIGHT weight,
                     ComPtr<IDWriteTextFormat>& out) {
        m_dwriteFactory->CreateTextFormat(font, nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"zh-CN", out.GetAddressOf());
    };
    mkfmt(L"Segoe UI", 21.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_titleFormat);
    mkfmt(L"Segoe UI", 14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_sectionFormat);
    mkfmt(L"Segoe UI", 13.f, DWRITE_FONT_WEIGHT_NORMAL,    m_bodyFormat);
    mkfmt(L"Segoe UI", 11.f, DWRITE_FONT_WEIGHT_NORMAL,    m_captionFormat);
    mkfmt(L"Segoe MDL2 Assets", 15.f, DWRITE_FONT_WEIGHT_NORMAL, m_iconFormat);
    if (m_iconFormat) {
        m_iconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_iconFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return true;
}

// ====================================================================
// 设备相关资源 (HwndRenderTarget + brush)
// ====================================================================
bool SettingsWindow::EnsureDeviceResources() {
    if (m_renderTarget) return true;
    if (!m_hwnd || !m_d2dFactory) return false;

    RECT clientRect{};
    GetClientRect(m_hwnd, &clientRect);
    UINT clientWidth = (UINT)(clientRect.right - clientRect.left);
    UINT clientHeight = (UINT)(clientRect.bottom - clientRect.top);

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(clientWidth, clientHeight));

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(rtp, hrtp, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), m_solidBrush.GetAddressOf());
    m_renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    m_renderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
    return true;
}

void SettingsWindow::DiscardDeviceResources() {
    m_solidBrush.Reset();
    m_renderTarget.Reset();
}

void SettingsWindow::ResizeRenderTarget(UINT w, UINT h) {
    if (m_renderTarget) {
        m_renderTarget->Resize(D2D1::SizeU(w, h));
        m_renderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
    }
}

// ====================================================================
// Show / Hide / Toggle
// ====================================================================
void SettingsWindow::Show() {
    if (!m_hwnd) return;
    CenterToParent();
    SwitchCategory(m_currentCategory);
    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_visible = true;
    m_hidePending = false;
    m_windowAlpha.SetTarget(1.f);
    StartAnimationTimer();
}

void SettingsWindow::Hide() {
    if (!m_hwnd || !m_visible) return;
    m_hidePending = true;
    m_windowAlpha.SetTarget(0.f);
    StartAnimationTimer();
}

void SettingsWindow::Toggle() {
    if (m_visible && !m_hidePending) Hide(); else Show();
}

void SettingsWindow::CenterToParent() const {
    RECT work{};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    int physicalWidth = DipToPixels(WIN_W);
    int physicalHeight = DipToPixels(WIN_H);
    int x = work.left + (work.right  - work.left - physicalWidth) / 2;
    int y = work.top  + (work.bottom - work.top  - physicalHeight) / 2;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, physicalWidth, physicalHeight,
                 SWP_NOACTIVATE);
}

// ====================================================================
// 动画计时器
// ====================================================================
void SettingsWindow::StartAnimationTimer() {
    if (!m_hwnd) return;
    if (m_lastAnimationTick == 0) {
        m_lastAnimationTick = GetTickCount64();
    }
    SetTimer(m_hwnd, ANIM_TIMER, 16, nullptr);
}

void SettingsWindow::StopAnimationTimerIfIdle() {
    if (!HasActiveAnimations()) KillTimer(m_hwnd, ANIM_TIMER);
}

bool SettingsWindow::HasActiveAnimations() const {
    auto settled = [](const Spring& s) { return s.IsSettled(0.3f, 0.1f); };
    if (!settled(m_sidebarSelectionY)) return true;
    if (!settled(m_contentInAlpha))    return true;
    if (!settled(m_contentOutAlpha))   return true;
    if (!settled(m_contentInOffsetY))  return true;
    if (!settled(m_contentOutOffsetY)) return true;
    if (!settled(m_scrollSpring))      return true;
    if (!settled(m_windowAlpha))       return true;
    if (!settled(m_statusAlpha))       return true;
    if (!settled(m_closeButtonHover))  return true;
    for (auto& s : m_navHoverSprings)  if (!settled(s)) return true;
    for (auto& s : m_trafficLightHoverSprings) if (!settled(s)) return true;
    for (auto& c : m_activeControls) {
        if (!settled(c.spring))      return true;
        if (!settled(c.hoverSpring)) return true;
    }
    for (auto& c : m_outgoingControls) {
        if (!settled(c.spring))      return true;
        if (!settled(c.hoverSpring)) return true;
    }
    for (auto& c : m_footerControls) {
        if (!settled(c.spring))      return true;
        if (!settled(c.hoverSpring)) return true;
    }
    return false;
}

bool SettingsWindow::TickAnimations(float dt) {
    auto tick = [&](Spring& s) { s.Update(dt); };
    tick(m_sidebarSelectionY);
    tick(m_contentInAlpha);
    tick(m_contentOutAlpha);
    tick(m_contentInOffsetY);
    tick(m_contentOutOffsetY);
    tick(m_scrollSpring);
    tick(m_windowAlpha);
    tick(m_statusAlpha);
    tick(m_closeButtonHover);
    for (auto& s : m_navHoverSprings) tick(s);
    for (auto& s : m_trafficLightHoverSprings) tick(s);
    for (auto& c : m_activeControls)  { tick(c.spring); tick(c.hoverSpring); }
    for (auto& c : m_outgoingControls){ tick(c.spring); tick(c.hoverSpring); }
    for (auto& c : m_footerControls)  { tick(c.spring); tick(c.hoverSpring); }

    // 离场完成后清空旧控件
    if (m_contentOutAlpha.IsSettled(0.02f, 0.05f) &&
        m_contentOutAlpha.GetValue() < 0.02f) {
        m_outgoingControls.clear();
    }
    // 隐藏等待
    if (m_hidePending && m_windowAlpha.IsSettled(0.02f, 0.05f) &&
        m_windowAlpha.GetValue() < 0.02f) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible    = false;
        m_hidePending = false;
        m_lastAnimationTick = 0;
    }
    return HasActiveAnimations();
}

bool SettingsWindow::UpdateControlAnimations(std::vector<SettingsControl>& controls, float dt) {
    bool active = false;
    for (auto& c : controls) {
        c.spring.Update(dt);
        c.hoverSpring.Update(dt);
        c.animatedValue = c.spring.GetValue();
        if (!c.spring.IsSettled() || !c.hoverSpring.IsSettled()) active = true;
    }
    return active;
}

// ====================================================================
// 设置读写
// ====================================================================
std::wstring SettingsWindow::ResolveConfigPath() const {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    auto pos = path.rfind(L'\\');
    if (pos != std::wstring::npos) path = path.substr(0, pos + 1);
    return path;
}

std::wstring SettingsWindow::GetSettingsPath() const {
    return GetConfigPath();
}

std::wstring SettingsWindow::GetConfigPath() const {
    return m_configPath + CONFIG_FILE;
}

bool SettingsWindow::GetSystemDarkMode() {
    HKEY hKey;
    DWORD val = 0, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
        return val == 0;
    }
    return false;
}

void SettingsWindow::LoadSettings() {
    const std::wstring sp = GetSettingsPath();
    const std::wstring cp = GetConfigPath();

    auto getI = [](const wchar_t* sec, const wchar_t* key, int def, const std::wstring& path) {
        return GetPrivateProfileIntW(sec, key, def, path.c_str());
    };
    auto getF = [&](const wchar_t* sec, const wchar_t* key, float def, const std::wstring& path) {
        return float(getI(sec, key, int(def), path));
    };
    auto getS = [](const wchar_t* sec, const wchar_t* key, const wchar_t* def, const std::wstring& path) {
        wchar_t buf[512] = {};
        GetPrivateProfileStringW(sec, key, def, buf, 512, path.c_str());
        return std::wstring(buf);
    };

    m_darkMode          = getI(L"Settings",  L"DarkMode",          0,   sp) != 0;
    m_followSystemTheme = getI(L"Settings",  L"FollowSystemTheme", 1,   sp) != 0;
    m_mainUIWidth       = getF(L"MainUI",    L"Width",             400, sp);
    m_mainUIHeight      = getF(L"MainUI",    L"Height",            200, sp);
    m_mainUITransparency= getI(L"MainUI",    L"Transparency",      100, sp) / 100.f;
    m_filePanelWidth    = getF(L"FilePanel", L"Width",             340, sp);
    m_filePanelHeight   = getF(L"FilePanel", L"Height",            240, sp);
    m_filePanelTransparency = getI(L"FilePanel", L"Transparency",  90,  sp) / 100.f;
    m_springStiffness   = getF(L"Advanced",  L"SpringStiffness",   100, sp);
    m_springDamping     = getF(L"Advanced",  L"SpringDamping",     10,  sp);
    m_autoStart           = AutoStart::IsEnabled();
    m_facePerformanceMode = getI(L"FaceUnlock", L"PerformanceMode",   0,   cp) != 0;
    m_lowBatteryThreshold = getF(L"Advanced",  L"LowBatteryThreshold",20,  cp);
    m_fileStashMaxItems   = getF(L"Advanced",  L"FileStashMaxItems",  5,   cp);
    m_mediaPollIntervalMs = getF(L"Advanced",  L"MediaPollIntervalMs",1000,cp);

    m_weatherCity         = ReadUtf8IniValue(cp, L"Weather", L"City", getS(L"Weather", L"City", L"北京", cp));
    m_weatherApiKey       = getS(L"Weather",   L"APIKey",     L"",           cp);
    m_weatherLocationId   = getS(L"Weather",   L"LocationId", L"101010100",  cp);
    m_allowedApps         = getS(L"Notifications", L"AllowedApps", L"微信,QQ", cp);

    LoadCompactModeOrder(ReadUtf8IniValue(cp, L"MainUI", L"CompactModeOrder", L"Music,Pomodoro,Todo,Agent"));
    m_compactAlbumVinyl = IsVinylStyle(ReadUtf8IniValue(cp, L"MainUI", L"CompactAlbumArtStyle", L"Vinyl"), true);
    m_expandedAlbumVinyl = IsVinylStyle(ReadUtf8IniValue(cp, L"MainUI", L"ExpandedAlbumArtStyle", L"Square"), false);
    m_allowedApps = ReadUtf8IniValue(cp, L"Notifications", L"AllowedApps", m_allowedApps);
    if (m_followSystemTheme) m_darkMode = GetSystemDarkMode();
    m_isDirty = false;
}

void SettingsWindow::SaveSettings() {
    const std::wstring sp = GetSettingsPath();
    const std::wstring cp = GetConfigPath();
    wchar_t b[64];

    auto wI = [&](const wchar_t* sec, const wchar_t* key, int val, const std::wstring& path) {
        swprintf_s(b, L"%d", val);
        WritePrivateProfileStringW(sec, key, b, path.c_str());
    };

    wI(L"Settings",  L"DarkMode",          m_darkMode ? 1 : 0,              sp);
    wI(L"Settings",  L"FollowSystemTheme",  m_followSystemTheme ? 1 : 0,     sp);
    wI(L"Settings",  L"AutoStart",         m_autoStart ? 1 : 0,             sp);
    wI(L"Settings",  L"AutoStart",         m_autoStart ? 1 : 0,             cp);
    wI(L"FaceUnlock", L"PerformanceMode",  m_facePerformanceMode ? 1 : 0,   cp);
    wI(L"MainUI",    L"Width",              int(m_mainUIWidth),               sp);
    wI(L"MainUI",    L"Height",             int(m_mainUIHeight),              sp);
    wI(L"MainUI",    L"Transparency",       int(m_mainUITransparency * 100),  sp);
    wI(L"FilePanel", L"Width",              int(m_filePanelWidth),            sp);
    wI(L"FilePanel", L"Height",             int(m_filePanelHeight),           sp);
    wI(L"FilePanel", L"Transparency",       int(m_filePanelTransparency*100), sp);
    wI(L"Advanced",  L"SpringStiffness",    int(m_springStiffness),           sp);
    wI(L"Advanced",  L"SpringDamping",      int(m_springDamping),             sp);
    wI(L"Advanced",  L"LowBatteryThreshold", int(m_lowBatteryThreshold),      sp);
    wI(L"Advanced",  L"FileStashMaxItems",   int(m_fileStashMaxItems),        sp);
    wI(L"Advanced",  L"MediaPollIntervalMs", int(m_mediaPollIntervalMs),      sp);
    WritePrivateProfileStringW(L"Notifications", L"AllowedApps",
        m_allowedApps.c_str(), sp.c_str());

    WritePrivateProfileStringW(L"Weather", L"City",
        m_weatherCity.c_str(), cp.c_str());
    WritePrivateProfileStringW(L"Weather", L"APIKey",
        m_weatherApiKey.c_str(), cp.c_str());
    WritePrivateProfileStringW(L"Weather", L"LocationId",
        m_weatherLocationId.c_str(), cp.c_str());
    WriteUtf8IniValue(cp, L"Weather", L"City", m_weatherCity);
    WriteUtf8IniValue(cp, L"Weather", L"APIKey", m_weatherApiKey);
    WriteUtf8IniValue(cp, L"Weather", L"LocationId", m_weatherLocationId);
    WriteUtf8IniValue(cp, L"Notifications", L"AllowedApps", m_allowedApps);
    WriteUtf8IniValue(cp, L"MainUI", L"CompactModeOrder", SerializeCompactModeOrder());
    WriteUtf8IniValue(cp, L"MainUI", L"CompactAlbumArtStyle", m_compactAlbumVinyl ? L"Vinyl" : L"Square");
    WriteUtf8IniValue(cp, L"MainUI", L"ExpandedAlbumArtStyle", m_expandedAlbumVinyl ? L"Vinyl" : L"Square");
}

void SettingsWindow::ApplySettings() {
    SaveSettings();
    AutoStart::SetEnabled(m_autoStart);
    if (m_parentHwnd)
        PostMessageW(m_parentHwnd, WM_SETTINGS_APPLY, 0, 0);
}

size_t SettingsWindow::GetFaceTemplateCount() const {
    try {
        face_core::FaceTemplateStore store;
        store.Load();
        return store.Count();
    } catch (...) {
        return 0;
    }
}

std::wstring SettingsWindow::GetFaceUnlockStatusText() const {
    const size_t count = GetFaceTemplateCount();
    if (count == 0) {
        return L"未录入";
    }
    return L"已录入 " + std::to_wstring(count) + L" 个模板";
}

std::wstring SettingsWindow::SerializeCompactModeOrder() const {
    std::wstring rawValue;
    bool first = true;
    for (const auto& modeKey : m_compactModeOrder) {
        if (!first) {
            rawValue += L",";
        }
        rawValue += modeKey;
        first = false;
    }
    return rawValue;
}

void SettingsWindow::LoadCompactModeOrder(const std::wstring& rawValue) {
    m_compactModeOrder.clear();

    size_t start = 0;
    while (start <= rawValue.size()) {
        const size_t comma = rawValue.find(L',', start);
        std::wstring token = rawValue.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        token.erase(0, token.find_first_not_of(L" \t\r\n"));
        if (!token.empty()) {
            token.erase(token.find_last_not_of(L" \t\r\n") + 1);
        }

        if (!token.empty() &&
            std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), token) == m_compactModeOrder.end()) {
            for (const auto& mode : kCompactModes) {
                if (_wcsicmp(token.c_str(), mode.key) == 0) {
                    m_compactModeOrder.push_back(mode.key);
                    break;
                }
            }
        }

        if (comma == std::wstring::npos) {
            break;
        }
        start = comma + 1;
    }

    for (const auto& mode : kCompactModes) {
        if (std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), mode.key) == m_compactModeOrder.end()) {
            m_compactModeOrder.push_back(mode.key);
        }
    }
}

bool SettingsWindow::IsCompactModeEnabled(const std::wstring& modeKey) const {
    return std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), modeKey) != m_compactModeOrder.end();
}

void SettingsWindow::ToggleCompactMode(const std::wstring& modeKey) {
    auto it = std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), modeKey);
    if (it != m_compactModeOrder.end()) {
        m_compactModeOrder.erase(it);
    } else {
        for (const auto& mode : kCompactModes) {
            if (_wcsicmp(mode.key, modeKey.c_str()) == 0) {
                m_compactModeOrder.push_back(mode.key);
                break;
            }
        }
    }
}

void SettingsWindow::MoveCompactMode(const std::wstring& modeKey, int delta) {
    auto it = std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), modeKey);
    if (it == m_compactModeOrder.end()) {
        return;
    }

    const ptrdiff_t index = std::distance(m_compactModeOrder.begin(), it);
    ptrdiff_t nextIndex = index + delta;
    if (nextIndex < 0 || nextIndex >= static_cast<ptrdiff_t>(m_compactModeOrder.size())) {
        return;
    }

    std::iter_swap(m_compactModeOrder.begin() + index, m_compactModeOrder.begin() + nextIndex);
}

void SettingsWindow::MarkDirty(bool dirty, const std::wstring& msg) {
    m_isDirty = dirty;
    if (!msg.empty()) {
        m_statusText = msg;
        m_statusAlpha.SetTarget(1.f);
    }
    for (auto& control : m_footerControls) {
        if (control.id == SettingID::BTN_SAVE || control.id == SettingID::BTN_APPLY) {
            control.enabled = m_isDirty;
            if (!control.enabled) {
                control.hovered = false;
                control.hoverSpring.SetTarget(0.0f);
            }
        }
    }
    StartAnimationTimer();
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ====================================================================
// 主题颜色
// ====================================================================
void SettingsWindow::UpdateTheme() {
    if (m_darkMode) {
        m_windowColor          = CF(0.071f, 0.071f, 0.082f);
        m_sidebarColor         = CF(0.094f, 0.094f, 0.106f);
        m_cardColor            = CF(0.153f, 0.153f, 0.173f);
        m_cardStrokeColor      = CF(0.255f, 0.255f, 0.282f, 0.8f);
        m_textColor            = CF(0.937f, 0.937f, 0.957f);
        m_secondaryTextColor   = CF(0.647f, 0.651f, 0.690f);
        m_hairlineColor        = CF(0.224f, 0.224f, 0.247f);
        m_sidebarSelectionColor= CF(0.220f, 0.251f, 0.322f);
        m_footerColor          = CF(0.098f, 0.098f, 0.114f);
        m_inputFillColor       = CF(0.196f, 0.200f, 0.224f);
    } else {
        m_windowColor          = CF(0.975f, 0.975f, 0.988f);
        m_sidebarColor         = CF(0.949f, 0.953f, 0.965f);
        m_cardColor            = CF(1.000f, 1.000f, 1.000f);
        m_cardStrokeColor      = CF(0.867f, 0.871f, 0.890f, 0.8f);
        m_textColor            = CF(0.110f, 0.110f, 0.118f);
        m_secondaryTextColor   = CF(0.455f, 0.455f, 0.502f);
        m_hairlineColor        = CF(0.886f, 0.886f, 0.906f);
        m_sidebarSelectionColor= CF(0.886f, 0.925f, 1.000f);
        m_footerColor          = CF(0.969f, 0.969f, 0.984f);
        m_inputFillColor       = CF(0.949f, 0.953f, 0.965f);
    }
    m_accentColor = CF(0.298f, 0.494f, 1.0f);
}

void SettingsWindow::ApplyTheme() {
    UpdateTheme();
    RefreshControlTargets();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::ResetToDefaults() {
    bool changed = true;
    switch (m_currentCategory) {
    case SettingCategory::General:
        m_darkMode = GetSystemDarkMode();
        m_followSystemTheme = true;
        m_autoStart = false;
        UpdateTheme();
        break;
    case SettingCategory::Appearance:
        changed = false;
        break;
    case SettingCategory::MainUI:
        m_mainUIWidth = 400.0f;
        m_mainUIHeight = 420.0f;
        m_mainUITransparency = 1.0f;
        m_compactAlbumVinyl = true;
        m_expandedAlbumVinyl = false;
        LoadCompactModeOrder(L"Music,Pomodoro,Todo,Agent");
        break;
    case SettingCategory::FilePanel:
        m_filePanelWidth = 340.0f;
        m_filePanelHeight = 200.0f;
        m_filePanelTransparency = 0.9f;
        break;
    case SettingCategory::Weather:
        m_weatherCity = L"北京";
        m_weatherApiKey.clear();
        m_weatherLocationId = L"101010100";
        m_citySearchActive = false;
        m_citySearchText.clear();
        m_filteredCities.clear();
        m_selectedCityIndex = -1;
        m_selectedRegion.clear();
        m_selectedPrefecture.clear();
        m_regionPickerOpen = false;
        m_regionPickerLevel = 0;
        m_isLocating = false;
        break;
    case SettingCategory::Notifications:
        m_allowedApps = L"微信,QQ";
        break;
    case SettingCategory::FaceUnlock:
        m_autoStart = false;
        m_facePerformanceMode = false;
        break;
    case SettingCategory::Agent:
        changed = false;
        break;
    case SettingCategory::Advanced:
        m_springStiffness = 100.0f;
        m_springDamping = 10.0f;
        m_lowBatteryThreshold = 20.0f;
        m_fileStashMaxItems = 5.0f;
        m_mediaPollIntervalMs = 1000.0f;
        break;
    case SettingCategory::About:
        changed = false;
        break;
    }

    if (!changed) {
        MarkDirty(m_isDirty, L"当前页面没有可恢复的默认项");
        return;
    }

    SwitchCategory(m_currentCategory);
    MarkDirty(true, L"已恢复当前页面默认值");
}

void SettingsWindow::RefreshControlTargets() {
    // 重建当前页（颜色已更新）
    SwitchCategory(m_currentCategory);
}

// ====================================================================
// 类别切换（带交叉淡化动画）
// ====================================================================
void SettingsWindow::SwitchCategory(SettingCategory category) {
    m_currentCategory = category;

    // 旧页离场
    if (!m_activeControls.empty()) {
        m_outgoingControls = std::move(m_activeControls);
        m_outgoingContentHeight = m_activeContentHeight;
        m_contentOutAlpha.SetTarget(0.f);
        m_contentOutOffsetY.SetTarget(-10.f);
    }

    // 新页入场
    m_activeControls.clear();
    m_activeContentHeight = 0.f;
    BuildPage(category, m_activeControls, m_activeContentHeight);
    RebuildFooterControls();

    m_contentInAlpha.SetTarget(0.f);
    m_contentInAlpha.SnapToTarget();
    m_contentInOffsetY.SetTarget(12.f);
    m_contentInOffsetY.SnapToTarget();
    m_contentInAlpha.SetTarget(1.f);
    m_contentInOffsetY.SetTarget(0.f);

    // 侧边栏指示器滑动
    float newY = NAV_Y0 + float(int(category)) * (NAV_H + NAV_GAP);
    m_sidebarSelectionY.SetTarget(newY);

    // 重置滚动
    RefreshScrollBounds(true);
    m_scrollSpring.SetTarget(0.f);
    m_scrollSpring.SnapToTarget();

    StartAnimationTimer();
}

void SettingsWindow::RefreshScrollBounds(bool snap) {
    float maxScroll = (std::max)(0.f, m_activeContentHeight - CONT_VH + 20.f);
    m_scrollSpring.SetTarget(std::clamp(m_scrollSpring.GetValue(), 0.f, maxScroll));
    if (snap) {
        m_scrollSpring.SetTarget(0.f);
        m_scrollSpring.SnapToTarget();
    }
}

float SettingsWindow::ClampScroll(float v) const {
    float maxScroll = (std::max)(0.f, m_activeContentHeight - CONT_VH + 20.f);
    return std::clamp(v, 0.f, maxScroll);
}

float SettingsWindow::GetTargetScroll() const {
    return m_scrollSpring.GetValue();
}

// ====================================================================
// 页面构建 — 公共入口
// ====================================================================
void SettingsWindow::BuildPage(SettingCategory cat,
                               std::vector<SettingsControl>& ctrls,
                               float& contentH) const {
    switch (cat) {
    case SettingCategory::General:       BuildGeneralPage      (ctrls, contentH); break;
    case SettingCategory::Appearance:    BuildAppearancePage   (ctrls, contentH); break;
    case SettingCategory::MainUI:        BuildMainUiPage       (ctrls, contentH); break;
    case SettingCategory::FilePanel:     BuildFilePanelPage    (ctrls, contentH); break;
    case SettingCategory::Weather:       BuildWeatherPage      (ctrls, contentH); break;
    case SettingCategory::Notifications: BuildNotificationsPage(ctrls, contentH); break;
    case SettingCategory::FaceUnlock:    BuildFaceUnlockPage   (ctrls, contentH); break;
    case SettingCategory::Agent:         BuildAgentPage        (ctrls, contentH); break;
    case SettingCategory::Advanced:      BuildAdvancedPage     (ctrls, contentH); break;
    case SettingCategory::About:         BuildAboutPage        (ctrls, contentH); break;
    }
}

// ---- 辅助：构建控件 --------------------------------------------------
static SettingsControl mkCard(float y, float h) {
    SettingsControl c; c.kind = ControlKind::Card;
    c.bounds = D2D1::RectF(0, y, CONT_W, y + h);
    return c;
}
static SettingsControl mkSep(float y) {
    SettingsControl c; c.kind = ControlKind::Separator;
    c.bounds = D2D1::RectF(16, y, CONT_W - 16, y + 1);
    return c;
}
static void mkToggle(std::vector<SettingsControl>& ctrls, int id,
                     const wchar_t* text, const wchar_t* sub,
                     bool val, float y) {
    SettingsControl c; c.kind = ControlKind::Toggle;
    c.id = id; c.text = text; c.subtitle = sub;
    c.value = val ? 1.f : 0.f;
    c.animatedValue = c.value;
    c.spring.SetStiffness(220.f); c.spring.SetDamping(22.f);
    c.spring.SetTarget(c.value); c.spring.SnapToTarget();
    c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
    c.bounds = D2D1::RectF(0, y, CONT_W, y + ROW_H);
    ctrls.push_back(c);
}
static void mkSlider(std::vector<SettingsControl>& ctrls, int id,
                     const wchar_t* text, const wchar_t* sub,
                     float rawVal, float minV, float maxV,
                     const wchar_t* suffix, float y) {
    SettingsControl c; c.kind = ControlKind::Slider;
    c.id = id; c.text = text; c.subtitle = sub;
    c.minVal = minV; c.maxVal = maxV; c.suffix = suffix;
    c.SetRawValue(rawVal);
    c.animatedValue = c.value;
    c.spring.SetStiffness(200.f); c.spring.SetDamping(22.f);
    c.spring.SetTarget(c.value); c.spring.SnapToTarget();
    c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
    c.bounds = D2D1::RectF(0, y, CONT_W, y + SLIDER_H);
    ctrls.push_back(c);
}
static void mkTextInput(std::vector<SettingsControl>& ctrls, int id,
                        const wchar_t* text, const wchar_t* sub,
                        const std::wstring& init, float y) {
    SettingsControl c; c.kind = ControlKind::TextInput;
    c.id = id; c.text = text; c.subtitle = sub;
    c.inputText = init; c.cursorPos = int(init.size());
    c.bounds = D2D1::RectF(0, y, CONT_W, y + INPUT_H);
    ctrls.push_back(c);
}

static void mkButton(std::vector<SettingsControl>& ctrls, int id,
                     const wchar_t* text, const std::wstring& valueText, float y) {
    SettingsControl c; c.kind = ControlKind::Button;
    c.id = id; c.text = text;
    c.valueText = valueText;
    c.bounds = D2D1::RectF(0, y, CONT_W, y + INPUT_H);
    c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
    ctrls.push_back(c);
}
static SettingsControl mkSubLabel(const wchar_t* text, float y) {
    SettingsControl c; c.kind = ControlKind::SubLabel;
    c.text = text;
    c.bounds = D2D1::RectF(16, y, CONT_W - 16, y + 36);
    return c;
}

// ====================================================================
// 页面: 通用
// ====================================================================
void SettingsWindow::BuildGeneralPage(std::vector<SettingsControl>& ctrls,
                                      float& h) const {
    float y = 0;
    // 卡片1: 深色模式 + 跟随系统
    float card1H = CARD_PAD + ROW_H + 1 + ROW_H + CARD_PAD;
    ctrls.push_back(mkCard(y, card1H));
    mkToggle(ctrls, SettingID::DARK_MODE,     L"深色模式",   L"立即预览设置窗口的明暗外观", m_darkMode,          y + CARD_PAD);
    ctrls.push_back(mkSep(y + CARD_PAD + ROW_H));
    mkToggle(ctrls, SettingID::FOLLOW_SYSTEM, L"跟随系统主题", L"与 Windows 明暗模式保持同步", m_followSystemTheme, y + CARD_PAD + ROW_H + 1);
    y += card1H + CARD_GAP;

    // 卡片2: 开机自启
    float card2H = CARD_PAD + ROW_H + CARD_PAD;
    ctrls.push_back(mkCard(y, card2H));
    mkToggle(ctrls, SettingID::AUTOSTART, L"开机自启动", L"系统启动时自动运行 Dynamic Island", m_autoStart, y + CARD_PAD);
    y += card2H + CARD_GAP;

    h = y;
}

// ====================================================================
// 页面: 外观
// ====================================================================
void SettingsWindow::BuildAppearancePage(std::vector<SettingsControl>& ctrls,
                                         float& h) const {
    float y = 0;
    float cardH = CARD_PAD + 60 + CARD_PAD;
    ctrls.push_back(mkCard(y, cardH));
    ctrls.push_back(mkSubLabel(L"当前已启用 macOS 风格双栏布局：左侧导航栏、圆角卡片、弹簧动画过渡。\n后续可在此配置强调色与字体缩放。", y + CARD_PAD));
    y += cardH + CARD_GAP;
    h = y;
}

// ====================================================================
// 页面: 主岛
// ====================================================================
void SettingsWindow::BuildMainUiPage(std::vector<SettingsControl>& ctrls,
                                     float& h) const {
    float y = 0;
    float cardH = CARD_PAD + SLIDER_H + 1 + SLIDER_H + 1 + SLIDER_H + CARD_PAD;
    ctrls.push_back(mkCard(y, cardH));
    float ry = y + CARD_PAD;
    mkSlider(ctrls, SettingID::MAIN_WIDTH,  L"主岛宽度",   L"胶囊展开态的最大宽度",  m_mainUIWidth,        200, 600, L" px", ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::MAIN_HEIGHT, L"主岛高度",   L"展开态高度（含音乐面板）",m_mainUIHeight,      100, 400, L" px", ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::MAIN_ALPHA,  L"主岛透明度", L"背景不透明度百分比",   m_mainUITransparency*100, 30, 100, L"%",  ry);
    y += cardH + CARD_GAP;
    float artworkCardH = 0.0f;
    BuildMusicArtworkCard(ctrls, y, artworkCardH);
    y += artworkCardH + CARD_GAP;
    float compactModesCardH = 0.0f;
    BuildCompactModesCard(ctrls, y, compactModesCardH);
    y += compactModesCardH + CARD_GAP;
    ctrls.push_back(mkSubLabel(L"Idle mode is always available as the final fallback.", y));
    y += 44.0f + CARD_GAP;
    ctrls.push_back(mkSubLabel(L"Pomodoro sessions restore in paused state after restart.", y));
    y += 44.0f + CARD_GAP;
    h = y;
}

// ====================================================================
// 页面: 文件副岛
// ====================================================================
void SettingsWindow::BuildMusicArtworkCard(std::vector<SettingsControl>& ctrls, float y, float& cardHeight) const {
    const float rowHeight = 42.0f;
    const float titleHeight = 48.0f;
    cardHeight = CARD_PAD + titleHeight + rowHeight * 2.0f + CARD_PAD;
    ctrls.push_back(mkCard(y, cardHeight));

    SettingsControl title;
    title.kind = ControlKind::Label;
    title.bounds = D2D1::RectF(0.0f, y + CARD_PAD, CONT_W, y + CARD_PAD + titleHeight);
    title.text = L"Music Artwork";
    title.subtitle = L"Switch album art between square cover and vinyl record.";
    ctrls.push_back(title);

    struct ArtworkRow {
        int squareId;
        int vinylId;
        const wchar_t* label;
        bool vinyl;
    };
    const ArtworkRow rows[] = {
        { SettingID::MUSIC_COMPACT_ART_SQUARE, SettingID::MUSIC_COMPACT_ART_VINYL, L"Compact music", m_compactAlbumVinyl },
        { SettingID::MUSIC_EXPANDED_ART_SQUARE, SettingID::MUSIC_EXPANDED_ART_VINYL, L"Expanded music", m_expandedAlbumVinyl },
    };

    float rowTop = y + CARD_PAD + titleHeight;
    for (const auto& row : rows) {
        SettingsControl label;
        label.kind = ControlKind::Label;
        label.bounds = D2D1::RectF(0.0f, rowTop, CONT_W - 180.0f, rowTop + rowHeight);
        label.text = row.label;
        label.subtitle = row.vinyl ? L"Vinyl record" : L"Square cover";
        ctrls.push_back(label);

        const float segmentTop = rowTop + 6.0f;
        const float segmentHeight = 28.0f;
        const float segmentWidth = 64.0f;
        const float segmentGap = 5.0f;
        const float segmentLeft = CONT_W - 10.0f - segmentWidth * 2.0f - segmentGap;

        SettingsControl square = {};
        square.kind = ControlKind::Button;
        square.id = row.squareId;
        square.text = L"Square";
        square.isPrimary = !row.vinyl;
        square.bounds = D2D1::RectF(segmentLeft, segmentTop, segmentLeft + segmentWidth, segmentTop + segmentHeight);
        square.hoverSpring.SetStiffness(200.f);
        square.hoverSpring.SetDamping(24.f);
        ctrls.push_back(square);

        SettingsControl vinyl = {};
        vinyl.kind = ControlKind::Button;
        vinyl.id = row.vinylId;
        vinyl.text = L"Vinyl";
        vinyl.isPrimary = row.vinyl;
        vinyl.bounds = D2D1::RectF(segmentLeft + segmentWidth + segmentGap, segmentTop,
            segmentLeft + segmentWidth * 2.0f + segmentGap, segmentTop + segmentHeight);
        vinyl.hoverSpring.SetStiffness(200.f);
        vinyl.hoverSpring.SetDamping(24.f);
        ctrls.push_back(vinyl);
        rowTop += rowHeight;
    }
}

void SettingsWindow::BuildCompactModesCard(std::vector<SettingsControl>& ctrls, float y, float& cardHeight) const {
    const float rowHeight = 42.0f;
    const float titleHeight = 48.0f;
    cardHeight = CARD_PAD + titleHeight + rowHeight * static_cast<float>(sizeof(kCompactModes) / sizeof(kCompactModes[0])) + CARD_PAD;
    ctrls.push_back(mkCard(y, cardHeight));

    SettingsControl title;
    title.kind = ControlKind::Label;
    title.bounds = D2D1::RectF(0.0f, y + CARD_PAD, CONT_W, y + CARD_PAD + titleHeight);
    title.text = L"Compact Modes";
    title.subtitle = L"Enable, disable, and reorder compact-mode scroll targets.";
    ctrls.push_back(title);

    float rowTop = y + CARD_PAD + titleHeight;
    for (const auto& mode : kCompactModes) {
        const bool enabled = IsCompactModeEnabled(mode.key);
        const auto it = std::find(m_compactModeOrder.begin(), m_compactModeOrder.end(), mode.key);
        const ptrdiff_t index = it == m_compactModeOrder.end() ? -1 : std::distance(m_compactModeOrder.begin(), it);

        SettingsControl label;
        label.kind = ControlKind::Label;
        label.bounds = D2D1::RectF(0.0f, rowTop, CONT_W - 200.0f, rowTop + rowHeight);
        label.text = mode.label;
        label.subtitle = enabled ? mode.subtitle : L"Disabled in compact rotation";
        ctrls.push_back(label);

        SettingsControl toggle = {};
        toggle.kind = ControlKind::Button;
        toggle.id = mode.toggleId;
        toggle.text = enabled ? L"On" : L"Off";
        toggle.bounds = D2D1::RectF(CONT_W - 192.0f, rowTop + 6.0f, CONT_W - 134.0f, rowTop + 34.0f);
        ctrls.push_back(toggle);

        SettingsControl up = {};
        up.kind = ControlKind::Button;
        up.id = mode.upId;
        up.text = L"Up";
        up.enabled = enabled && index > 0;
        up.bounds = D2D1::RectF(CONT_W - 126.0f, rowTop + 6.0f, CONT_W - 72.0f, rowTop + 34.0f);
        ctrls.push_back(up);

        SettingsControl down = {};
        down.kind = ControlKind::Button;
        down.id = mode.downId;
        down.text = L"Down";
        down.enabled = enabled && index >= 0 && index < static_cast<ptrdiff_t>(m_compactModeOrder.size()) - 1;
        down.bounds = D2D1::RectF(CONT_W - 64.0f, rowTop + 6.0f, CONT_W - 10.0f, rowTop + 34.0f);
        ctrls.push_back(down);

        rowTop += rowHeight;
    }
}

void SettingsWindow::BuildFilePanelPage(std::vector<SettingsControl>& ctrls,
                                        float& h) const {
    float y = 0;
    float cardH = CARD_PAD + SLIDER_H + 1 + SLIDER_H + 1 + SLIDER_H + CARD_PAD;
    ctrls.push_back(mkCard(y, cardH));
    float ry = y + CARD_PAD;
    mkSlider(ctrls, SettingID::FILE_WIDTH,  L"文件副岛宽度",   L"展开面板的水平尺寸",   m_filePanelWidth,         200, 520, L" px", ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::FILE_HEIGHT, L"文件副岛高度",   L"展开面板的垂直尺寸",   m_filePanelHeight,        150, 420, L" px", ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::FILE_ALPHA,  L"文件副岛透明度", L"背景不透明度百分比",   m_filePanelTransparency*100, 30, 100, L"%",  ry);
    y += cardH + CARD_GAP;
    ctrls.push_back(mkSubLabel(L"Stashed file list restores after app restart.", y));
    y += 44.0f + CARD_GAP;
    h = y;
}

// ====================================================================
// 页面: 天气
// ====================================================================
void SettingsWindow::BuildWeatherPage(std::vector<SettingsControl>& ctrls,
                                      float& h) const {
    float y = 0;

    // 卡片: 城市选择与定位
    float card1H = CARD_PAD + INPUT_H + 1 + ROW_H + CARD_PAD;
    ctrls.push_back(mkCard(y, card1H));
    float ry = y + CARD_PAD;

    // 搜索输入框：输入文字持久化在 m_citySearchText，不依赖重建中的控件列表
    std::wstring sub = m_citySearchActive ? L"请输入城市中文名或拼音" : L"当前: " + m_weatherCity;
    mkTextInput(ctrls, SettingID::WEATHER_CITY, L"搜索城市", sub.c_str(),
                m_citySearchActive ? m_citySearchText : L"",
                ry);
    ry += INPUT_H + 1;

    // 自动定位按钮 (Row 样式按钮)
    {
        SettingsControl c; c.kind = ControlKind::Button;
        c.id = SettingID::WEATHER_AUTO_LOC;
        c.text = m_isLocating ? L"正在定位..." : L"🔵 自动定位当前位置";
        c.bounds = D2D1::RectF(0, ry, CONT_W, ry + ROW_H);
        c.enabled = !m_isLocating;
        c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
        ctrls.push_back(c);
    }
    y += card1H + CARD_GAP;

    // 卡片: API 配置
    float card2H = CARD_PAD + INPUT_H + 1 + INPUT_H + CARD_PAD;
    ctrls.push_back(mkCard(y, card2H));
    ry = y + CARD_PAD;

    mkTextInput(ctrls, SettingID::WEATHER_KEY, L"API Key", L"和风天气开发版或企业版密钥", m_weatherApiKey, ry); ry += INPUT_H + 1;

    // 位置 ID (只读展示)
    {
        SettingsControl c; c.kind = ControlKind::TextInput;
        c.id = SettingID::WEATHER_LOC;
        c.text = L"位置 ID";
        c.subtitle = L"选择城市后自动填入";
        c.inputText = m_weatherLocationId;
        c.enabled = false; // 只读
        c.bounds = D2D1::RectF(0, ry, CONT_W, ry + INPUT_H);
        ctrls.push_back(c);
    }
    y += card2H + CARD_GAP;

    ctrls.push_back(mkSubLabel(L"API Key 可在 qweather.com 获取。支持自动定位或手动搜索全国 3000+ 城市。", y));
    y += 44 + CARD_GAP;
    ctrls.push_back(mkSubLabel(L"If API Key is empty, the island stays in an unavailable state and skips weather requests.", y));
    y += 44.0f + CARD_GAP;
    h = y;
}
// ====================================================================
// 页面: 通知
// ====================================================================
void SettingsWindow::BuildNotificationsPage(std::vector<SettingsControl>& ctrls,
                                            float& h) const {
    float y = 0;
    float cardH = CARD_PAD + INPUT_H + CARD_PAD;
    ctrls.push_back(mkCard(y, cardH));
    mkTextInput(ctrls, SettingID::NOTIFY_APPS, L"允许的应用",
                L"用英文逗号分隔，例：微信,QQ,飞书", m_allowedApps, y + CARD_PAD);
    y += cardH + CARD_GAP;

    ctrls.push_back(mkSubLabel(L"只有在此列表中的应用发出通知时，Dynamic Island 才会弹出通知卡片。", y));
    y += 44 + CARD_GAP;
    h = y;
}

void SettingsWindow::BuildFaceUnlockPage(std::vector<SettingsControl>& ctrls,
                                         float& h) const {
    float y = 0.0f;
    const size_t templateCount = GetFaceTemplateCount();

    float statusCardH = CARD_PAD + ROW_H + 1.0f + ROW_H + CARD_PAD;
    ctrls.push_back(mkCard(y, statusCardH));
    mkToggle(ctrls, SettingID::FACE_ENABLE, L"启用人脸解锁",
        L"锁屏 Credential Provider 将在 Phase 3 接入", false, y + CARD_PAD);
    if (!ctrls.empty()) {
        ctrls.back().enabled = false;
    }
    ctrls.push_back(mkSep(y + CARD_PAD + ROW_H));
    SettingsControl status;
    status.kind = ControlKind::Label;
    status.text = L"录入状态: " + GetFaceUnlockStatusText();
    status.bounds = D2D1::RectF(16.0f, y + CARD_PAD + ROW_H + 14.0f, CONT_W - 16.0f,
        y + CARD_PAD + ROW_H + 14.0f + 24.0f);
    ctrls.push_back(status);
    y += statusCardH + CARD_GAP;

    float actionCardH = CARD_PAD + INPUT_H + 1.0f + INPUT_H + CARD_PAD;
    ctrls.push_back(mkCard(y, actionCardH));
    float ry = y + CARD_PAD;
    mkButton(ctrls, SettingID::FACE_ENROLL, templateCount > 0 ? L"重新录入人脸" : L"录入人脸", L"", ry);
    ry += INPUT_H + 1.0f;
    mkButton(ctrls, SettingID::FACE_DELETE, L"删除已录入人脸", L"", ry);
    if (!ctrls.empty()) {
        ctrls.back().enabled = templateCount > 0;
    }
    y += actionCardH + CARD_GAP;

    float optionsCardH = CARD_PAD + ROW_H + 1.0f + ROW_H + CARD_PAD;
    ctrls.push_back(mkCard(y, optionsCardH));
    ry = y + CARD_PAD;
    mkToggle(ctrls, SettingID::FACE_AUTOSTART, L"开机自启动",
        L"写入当前用户 Run 项，命令带 --autostart", m_autoStart, ry);
    ctrls.push_back(mkSep(y + CARD_PAD + ROW_H));
    ry += ROW_H + 1.0f;
    mkToggle(ctrls, SettingID::FACE_PERF_MODE, L"性能模式",
        L"默认关闭；录入仍强制执行 Silent-Face 与 3DDFA 姿态检查", m_facePerformanceMode, ry);
    y += optionsCardH + CARD_GAP;

    SettingsControl note;
    note.kind = ControlKind::SubLabel;
    note.text = L"Phase 2 不注册锁屏组件、不弹 UAC、不保存 Windows 密码。";
    note.bounds = D2D1::RectF(16.0f, y, CONT_W - 16.0f, y + 44.0f);
    ctrls.push_back(note);
    y += 44.0f + CARD_GAP;
    h = y;
}

// ====================================================================
// 页面: Claude / Agent
// ====================================================================
void SettingsWindow::BuildAgentPage(std::vector<SettingsControl>& ctrls,
                                    float& h) const {
    const ClaudeHookInstallStatus claudeStatus = ClaudeHookInstaller::DetectStatus();
    const CodexHookInstallStatus codexStatus = CodexHookInstaller::DetectStatus();
    float y = 0.0f;

    float statusCardH = CARD_PAD + 176.0f + CARD_PAD;
    ctrls.push_back(mkCard(y, statusCardH));
    ctrls.push_back(mkSubLabel(
        (L"Claude\n"
         L"Python: " + std::wstring(claudeStatus.pythonAvailable ? L"Ready" : L"Missing") +
         L"\nPipe: " + std::wstring(claudeStatus.pipeListening ? L"Listening" : L"Stopped") +
         L"\nHooks: " + std::wstring(claudeStatus.installed ? L"Installed" : L"Not installed") +
         L"\n\nCodex\n"
         L"Node: " + std::wstring(codexStatus.nodeAvailable ? L"Ready" : L"Missing") +
         L"\nPipe: " + std::wstring(codexStatus.pipeListening ? L"Listening" : L"Stopped") +
         L"\nHooks: " + std::wstring(codexStatus.installed ? L"Installed" : L"Not installed")).c_str(),
        y + CARD_PAD));
    y += statusCardH + CARD_GAP;

    float claudeActionCardH = CARD_PAD + INPUT_H + 1.0f + INPUT_H + 1.0f + INPUT_H + CARD_PAD;
    ctrls.push_back(mkCard(y, claudeActionCardH));
    float ry = y + CARD_PAD;
    mkButton(ctrls, SettingID::CLAUDE_INSTALL,   L"Install Hooks",   L"", ry); ry += INPUT_H + 1.0f;
    mkButton(ctrls, SettingID::CLAUDE_REINSTALL, L"Reinstall Hooks", L"", ry); ry += INPUT_H + 1.0f;
    mkButton(ctrls, SettingID::CLAUDE_UNINSTALL, L"Uninstall Hooks", L"", ry);
    y += claudeActionCardH + CARD_GAP;

    float codexActionCardH = CARD_PAD + INPUT_H + 1.0f + INPUT_H + 1.0f + INPUT_H + CARD_PAD;
    ctrls.push_back(mkCard(y, codexActionCardH));
    ry = y + CARD_PAD;
    mkButton(ctrls, SettingID::CODEX_INSTALL,   L"Install Codex Hooks",   L"", ry); ry += INPUT_H + 1.0f;
    mkButton(ctrls, SettingID::CODEX_REINSTALL, L"Reinstall Codex Hooks", L"", ry); ry += INPUT_H + 1.0f;
    mkButton(ctrls, SettingID::CODEX_UNINSTALL, L"Uninstall Codex Hooks", L"", ry);
    y += codexActionCardH + CARD_GAP;

    ctrls.push_back(mkSubLabel(
        L"Claude 会写入 ~/.claude/hooks 与 ~/.claude/settings.json；Codex 会写入 ~/.codex/hooks 与 ~/.codex/hooks.json。两者都只增改本应用自己的 hook 条目，不覆盖用户已有其他 hooks。",
        y));
    y += 52.0f + CARD_GAP;
    h = y;
}

// ====================================================================
// 页面: 高级
// ====================================================================
void SettingsWindow::BuildAdvancedPage(std::vector<SettingsControl>& ctrls,
                                       float& h) const {
    float y = 0;
    // 弹簧物理
    float c1H = CARD_PAD + SLIDER_H + 1 + SLIDER_H + CARD_PAD;
    ctrls.push_back(mkCard(y, c1H));
    float ry = y + CARD_PAD;
    mkSlider(ctrls, SettingID::ADV_STIFFNESS, L"弹簧刚度", L"值越大动画恢复越快（推荐 80-200）",  m_springStiffness, 20,  400, L"",  ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::ADV_DAMPING,   L"弹簧阻尼", L"值越大振荡越少（推荐 8-20）",         m_springDamping,   2,   40,  L"",  ry);
    y += c1H + CARD_GAP;

    // 系统行为
    float c2H = CARD_PAD + SLIDER_H + 1 + SLIDER_H + 1 + SLIDER_H + CARD_PAD;
    ctrls.push_back(mkCard(y, c2H));
    ry = y + CARD_PAD;
    mkSlider(ctrls, SettingID::ADV_LOW_BAT,  L"低电量阈值",   L"低于此电量时弹出提示",         m_lowBatteryThreshold, 5,  50, L"%", ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::ADV_FILE_MAX, L"文件暂存上限", L"文件副岛最多暂存几个文件（1-10）", m_fileStashMaxItems,  1,  10, L"",  ry); ry += SLIDER_H + 1;
    mkSlider(ctrls, SettingID::ADV_MEDIA_POLL, L"媒体轮询间隔", L"兜底轮询间隔（毫秒）", m_mediaPollIntervalMs, 250, 4000, L" ms", ry);
    y += c2H + CARD_GAP;
    h = y;
}

// ====================================================================
// 页面: 关于
// ====================================================================
void SettingsWindow::BuildAboutPage(std::vector<SettingsControl>& ctrls,
                                    float& h) const {
    float y = 0;
    float cardH = CARD_PAD + 120 + CARD_PAD;
    ctrls.push_back(mkCard(y, cardH));
    ctrls.push_back(mkSubLabel(
        L"Dynamic Island for Windows\n"
        L"版本 1.0 · C++17 / Win32 / Direct2D\n\n"
        L"将 iPhone 14 Pro 灵动岛体验移植到 Windows 桌面。\n"
        L"组件化架构，Direct2D 渲染，弹簧物理动画，0% CPU 空闲占用。",
        y + CARD_PAD));
    y += cardH + CARD_GAP;
    h = y;
}

// ====================================================================
// 底部按钮
// ====================================================================
void SettingsWindow::RebuildFooterControls() {
    m_footerControls.clear();
    bool isAbout = (m_currentCategory == SettingCategory::About || m_currentCategory == SettingCategory::Agent);
    if (isAbout) return;
    const bool canResetCurrentPage = (m_currentCategory != SettingCategory::Appearance && m_currentCategory != SettingCategory::Agent);
    constexpr float buttonGap = 12.0f;
    constexpr float resetWidth = 112.0f;
    constexpr float saveWidth = 104.0f;
    constexpr float applyWidth = 126.0f;
    constexpr float buttonHeight = 38.0f;
    const float applyLeft = CONT_W - applyWidth;
    const float saveLeft = applyLeft - buttonGap - saveWidth;
    const float resetLeft = 0.0f;

    {
        SettingsControl c; c.kind = ControlKind::Button;
        c.id = SettingID::BTN_RESET; c.text = L"恢复默认"; c.isPrimary = false;
        c.enabled = canResetCurrentPage;
        c.bounds = D2D1::RectF(resetLeft, 16.0f, resetLeft + resetWidth, 16.0f + buttonHeight);
        c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
        m_footerControls.push_back(c);
    }

    // 保存按钮
    {
        SettingsControl c; c.kind = ControlKind::Button;
        c.id = SettingID::BTN_SAVE; c.text = L"保存"; c.isPrimary = false;
        c.enabled = m_isDirty;
        c.bounds = D2D1::RectF(saveLeft, 16.0f, saveLeft + saveWidth, 16.0f + buttonHeight);
        c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
        m_footerControls.push_back(c);
    }
    // 保存并应用
    {
        SettingsControl c; c.kind = ControlKind::Button;
        c.id = SettingID::BTN_APPLY; c.text = L"保存并应用"; c.isPrimary = true;
        c.enabled = m_isDirty;
        c.bounds = D2D1::RectF(applyLeft, 16.0f, applyLeft + applyWidth, 16.0f + buttonHeight);
        c.hoverSpring.SetStiffness(200.f); c.hoverSpring.SetDamping(24.f);
        m_footerControls.push_back(c);
    }
}

// ====================================================================
// 设置值同步
// ====================================================================
void SettingsWindow::SyncStateFromControl(int id) {
    auto find = [&](std::vector<SettingsControl>& v) -> SettingsControl* {
        for (auto& c : v) if (c.id == id) return &c;
        return nullptr;
    };
    SettingsControl* ctrl = find(m_activeControls);
    if (!ctrl) return;

    switch (id) {
    case SettingID::DARK_MODE:
        m_darkMode = ctrl->value > 0.5f;
        m_followSystemTheme = false;
        UpdateTheme();
        SwitchCategory(m_currentCategory);
        break;
    case SettingID::FOLLOW_SYSTEM:
        m_followSystemTheme = ctrl->value > 0.5f;
        if (m_followSystemTheme) { m_darkMode = GetSystemDarkMode(); UpdateTheme(); SwitchCategory(m_currentCategory); }
        break;
    case SettingID::AUTOSTART:
        m_autoStart = ctrl->value > 0.5f;
        break;
    case SettingID::FACE_AUTOSTART:
        m_autoStart = ctrl->value > 0.5f;
        break;
    case SettingID::FACE_PERF_MODE:
        m_facePerformanceMode = ctrl->value > 0.5f;
        break;
    case SettingID::MAIN_WIDTH:       m_mainUIWidth          = ctrl->GetRawValue(); break;
    case SettingID::MAIN_HEIGHT:      m_mainUIHeight         = ctrl->GetRawValue(); break;
    case SettingID::MAIN_ALPHA:       m_mainUITransparency   = ctrl->GetRawValue() / 100.f; break;
    case SettingID::FILE_WIDTH:       m_filePanelWidth       = ctrl->GetRawValue(); break;
    case SettingID::FILE_HEIGHT:      m_filePanelHeight      = ctrl->GetRawValue(); break;
    case SettingID::FILE_ALPHA:       m_filePanelTransparency= ctrl->GetRawValue() / 100.f; break;
    case SettingID::WEATHER_CITY:
        // 搜索模式：不覆盖 m_weatherCity，只更新搜索文字和候选列表
        if (m_citySearchActive) {
            m_citySearchText = ctrl->inputText;
            UpdateCityFilter(ctrl->inputText);
        } else {
            m_weatherCity = ctrl->inputText;
        }
        break;
    case SettingID::WEATHER_KEY:      m_weatherApiKey        = ctrl->inputText; break;
    case SettingID::WEATHER_LOC:      m_weatherLocationId    = ctrl->inputText; break;
    case SettingID::NOTIFY_APPS:      m_allowedApps          = ctrl->inputText; break;
    case SettingID::ADV_STIFFNESS:    m_springStiffness      = ctrl->GetRawValue(); break;
    case SettingID::ADV_DAMPING:      m_springDamping        = ctrl->GetRawValue(); break;
    case SettingID::ADV_LOW_BAT:      m_lowBatteryThreshold  = ctrl->GetRawValue(); break;
    case SettingID::ADV_FILE_MAX:     m_fileStashMaxItems    = ctrl->GetRawValue(); break;
    case SettingID::ADV_MEDIA_POLL:   m_mediaPollIntervalMs  = ctrl->GetRawValue(); break;
    }
    MarkDirty(true, L"已修改，等待保存");
}

void SettingsWindow::SyncControlFromState(SettingsControl& ctrl) const {
    // 仅用于初始化，BuildPage 中已直接读取 m_ 值
}

SettingsControl* SettingsWindow::FindControlById(
    std::vector<SettingsControl>& v, int id) {
    for (auto& c : v) if (c.id == id) return &c;
    return nullptr;
}

const SettingsControl* SettingsWindow::FindControlById(
    const std::vector<SettingsControl>& v, int id) const {
    for (const auto& c : v) if (c.id == id) return &c;
    return nullptr;
}

// ====================================================================
// 输入坐标转换（物理像素 → DIP）
// ====================================================================
float SettingsWindow::PixelsToDipX(int x) const { return float(x) / m_dpiScale; }
float SettingsWindow::PixelsToDipY(int y) const { return float(y) / m_dpiScale; }
int SettingsWindow::DipToPixels(float value) const { return int(std::lround(value * m_dpiScale)); }

// ---- 将窗口鼠标坐标转换为内容区相对坐标 ------
static bool InContentArea(float wx, float wy, float scrollY,
                           float& cx, float& cy) {
    if (wx < CONT_L || wx > float(WIN_W) - CONT_PAD) return false;
    if (wy < CONT_T || wy > CONT_B) return false;
    cx = wx - CONT_L;
    cy = wy - CONT_T + scrollY;
    return true;
}
static bool InSidebarArea(float wx, float wy) {
    return wx >= 0 && wx <= SIDEBAR && wy >= 0 && wy <= float(WIN_H);
}
static bool InFooterArea(float wx, float wy) {
    return wy >= CONT_B;
}

// ====================================================================
// 命中测试 — 内容区控件
// ====================================================================
SettingsControl* SettingsWindow::HitTestInteractiveControl(float wx, float wy) {
    float scrollY = m_scrollSpring.GetValue();
    float cx, cy;
    if (!InContentArea(wx, wy, scrollY, cx, cy)) return nullptr;
    for (auto& c : m_activeControls) {
        if (c.kind == ControlKind::Card || c.kind == ControlKind::Separator ||
            c.kind == ControlKind::SubLabel || c.kind == ControlKind::Label) continue;
        if (!c.enabled) continue;
        if (cx >= c.bounds.left && cx <= c.bounds.right &&
            cy >= c.bounds.top  && cy <= c.bounds.bottom)
            return &c;
    }
    return nullptr;
}
const SettingsControl* SettingsWindow::HitTestInteractiveControl(float wx, float wy) const {
    return const_cast<SettingsWindow*>(this)->HitTestInteractiveControl(wx, wy);
}

int SettingsWindow::HitTestNavigation(float wx, float wy) const {
    if (!InSidebarArea(wx, wy)) return -1;
    for (int i = 0; i < kCatCount; ++i) {
        float ny = NAV_Y0 + i * (NAV_H + NAV_GAP);
        if (wy >= ny && wy < ny + NAV_H && wx >= NAV_X && wx < NAV_X + NAV_W)
            return i;
    }
    return -1;
}

int SettingsWindow::HitTestTrafficLight(float wx, float wy) const {
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F rect = GetTrafficLightRect(i);
        float centerX = (rect.left + rect.right) * 0.5f;
        float centerY = (rect.top + rect.bottom) * 0.5f;
        float radius = (rect.right - rect.left) * 0.5f;
        float dx = wx - centerX;
        float dy = wy - centerY;
        if (dx * dx + dy * dy <= radius * radius) return i;
    }
    return -1;
}

D2D1_RECT_F SettingsWindow::GetCloseButtonRect() const {
    constexpr float buttonSize = 18.0f;
    constexpr float inset = 18.0f;
    float right = float(WIN_W) - inset;
    float top = (TITLEBAR - buttonSize) * 0.5f;
    return D2D1::RectF(right - buttonSize, top, right, top + buttonSize);
}

// ====================================================================
// 鼠标输入
// ====================================================================
bool SettingsWindow::UpdateHoverState(float wx, float wy) {
    bool changed = false;
    float scrollY = m_scrollSpring.GetValue();

    // 导航 hover
    int navIdx = HitTestNavigation(wx, wy);
    for (int i = 0; i < kCatCount; ++i)
        m_navHoverSprings[i].SetTarget(i == navIdx ? 1.f : 0.f);
    changed = changed || (m_hoveredNavIndex != navIdx);
    m_hoveredNavIndex = navIdx;

    // 交通灯 hover
    int trafficLight = HitTestTrafficLight(wx, wy);
    for (int i = 0; i < 3; ++i) {
        m_trafficLightHoverSprings[i].SetTarget(i == trafficLight ? 1.f : 0.f);
    }
    changed = changed || (m_hoveredTrafficLight != trafficLight);
    m_hoveredTrafficLight = trafficLight;

    D2D1_RECT_F closeRect = GetCloseButtonRect();
    bool hoverClose = wx >= closeRect.left && wx <= closeRect.right &&
        wy >= closeRect.top && wy <= closeRect.bottom;
    changed = changed || (m_hoveredCloseButton != hoverClose);
    m_hoveredCloseButton = hoverClose;
    m_closeButtonHover.SetTarget(hoverClose ? 1.0f : 0.0f);

    // 内容区控件 hover
    float cx, cy;
    bool inContent = InContentArea(wx, wy, scrollY, cx, cy);
    m_hoveredControlId = 0;
    for (auto& c : m_activeControls) {
        bool hit = inContent && c.enabled &&
                   cx >= c.bounds.left && cx <= c.bounds.right &&
                   cy >= c.bounds.top  && cy <= c.bounds.bottom;
        bool wasHovered = c.hovered;
        c.hovered = hit;
        if (hit && !wasHovered) c.hoverSpring.SetTarget(1.f);
        if (!hit && wasHovered) c.hoverSpring.SetTarget(0.f);
        if (hit) m_hoveredControlId = c.id;
        changed = changed || (wasHovered != hit);
    }
    // footer 按钮 hover
    for (auto& c : m_footerControls) {
        float fx = wx - CONT_L, fy = wy - CONT_B;
        bool hit = wy >= CONT_B && c.enabled &&
                   fx >= c.bounds.left && fx <= c.bounds.right &&
                   fy >= c.bounds.top  && fy <= c.bounds.bottom;
        bool wasHovered = c.hovered;
        c.hovered = hit;
        if (hit && !wasHovered) c.hoverSpring.SetTarget(1.f);
        if (!hit && wasHovered) c.hoverSpring.SetTarget(0.f);
        changed = changed || (wasHovered != hit);
    }
    if (changed) {
        StartAnimationTimer();
    }
    return changed;
}

void SettingsWindow::BeginSliderDrag(int id, float wx) {
    SettingsControl* ctrl = FindControlById(m_activeControls, id);
    if (!ctrl || ctrl->kind != ControlKind::Slider) return;
    m_draggingSliderId = id;
    // 轨道 left 在窗口坐标系
    float trackLeft = CONT_L + ctrl->bounds.left + 16.f;
    float trackW    = TRACK_W;
    float newVal = (wx - trackLeft) / trackW;
    newVal = std::clamp(newVal, 0.f, 1.f);
    ctrl->value = newVal;
    ctrl->animatedValue = newVal;
    ctrl->spring.SetTarget(newVal); ctrl->spring.SnapToTarget();
    SyncStateFromControl(id);
}

void SettingsWindow::UpdateSliderDrag(float wx) {
    if (!m_draggingSliderId) return;
    SettingsControl* ctrl = FindControlById(m_activeControls, m_draggingSliderId);
    if (!ctrl) return;
    float trackLeft = CONT_L + ctrl->bounds.left + 16.f;
    float newVal = (wx - trackLeft) / TRACK_W;
    newVal = std::clamp(newVal, 0.f, 1.f);
    ctrl->value = newVal;
    ctrl->animatedValue = newVal;
    ctrl->spring.SetTarget(newVal); ctrl->spring.SnapToTarget();
    SyncStateFromControl(m_draggingSliderId);
    StartAnimationTimer();
}

void SettingsWindow::EndSliderDrag() {
    m_draggingSliderId = 0;
}

void SettingsWindow::HandleControlActivation(int id) {
    // Toggle
    SettingsControl* ctrl = FindControlById(m_activeControls, id);
    if (ctrl && ctrl->kind == ControlKind::Toggle) {
        ctrl->value = ctrl->value > 0.5f ? 0.f : 1.f;
        ctrl->spring.SetTarget(ctrl->value);
        SyncStateFromControl(id);
        StartAnimationTimer();
        return;
    }

    if (id == SettingID::WEATHER_CITY) {
        m_citySearchActive = true;
        m_regionPickerOpen = false;
        if (ctrl && ctrl->kind == ControlKind::TextInput && !ctrl->editing) {
            ctrl->editing = true;
            m_caretBlinkStart = GetTickCount64();
        }
        SetFocusedControl(id);
        PositionIMEWindow();
        UpdateCityFilter(m_citySearchText);
        StartAnimationTimer();
        return;
    }

    if (id == SettingID::WEATHER_REGION) {
        m_regionPickerOpen = !m_regionPickerOpen;
        m_filteredCities.clear();  // 关闭城市候选
        StartAnimationTimer();
        return;
    }

    if (id == SettingID::WEATHER_AUTO_LOC) {
        AutoLocateCity();
        return;
    }

    if (id == SettingID::MUSIC_COMPACT_ART_SQUARE ||
        id == SettingID::MUSIC_COMPACT_ART_VINYL ||
        id == SettingID::MUSIC_EXPANDED_ART_SQUARE ||
        id == SettingID::MUSIC_EXPANDED_ART_VINYL) {
        if (id == SettingID::MUSIC_COMPACT_ART_SQUARE) {
            m_compactAlbumVinyl = false;
        } else if (id == SettingID::MUSIC_COMPACT_ART_VINYL) {
            m_compactAlbumVinyl = true;
        } else if (id == SettingID::MUSIC_EXPANDED_ART_SQUARE) {
            m_expandedAlbumVinyl = false;
        } else {
            m_expandedAlbumVinyl = true;
        }
        ApplySettings();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, L"Music artwork style applied");
        return;
    }

    if (const CompactModeMeta* compactMode = FindCompactModeMetaById(id)) {
        if (id == compactMode->toggleId) {
            ToggleCompactMode(compactMode->key);
        } else if (id == compactMode->upId) {
            MoveCompactMode(compactMode->key, -1);
        } else if (id == compactMode->downId) {
            MoveCompactMode(compactMode->key, +1);
        }
        SwitchCategory(m_currentCategory);
        MarkDirty(true, L"Compact mode order updated");
        return;
    }

    if (id == SettingID::FACE_ENROLL) {
        FaceEnrollWindow enrollWindow;
        const FaceEnrollWindow::Result result = enrollWindow.ShowModal(m_hInstance, m_hwnd);
        SwitchCategory(m_currentCategory);
        if (result == FaceEnrollWindow::Result::Completed) {
            MarkDirty(false, L"人脸录入完成");
            if (m_parentHwnd) {
                auto* event = new FaceUnlockEvent();
                event->kind = FaceUnlockEventKind::Success;
                event->user = L"local_user";
                PostMessageW(m_parentHwnd, WM_FACE_UNLOCK_EVENT, 0, reinterpret_cast<LPARAM>(event));
            }
        } else if (result == FaceEnrollWindow::Result::Failed) {
            MarkDirty(false, L"人脸录入失败");
        } else {
            MarkDirty(false, L"已取消人脸录入");
        }
        return;
    }

    if (id == SettingID::FACE_DELETE) {
        if (MessageBoxW(m_hwnd, L"删除当前用户已录入的人脸模板？", L"人脸解锁",
                MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES) {
            try {
                face_core::FaceTemplateStore store;
                store.Load();
                store.Clear();
                store.Save();
                SwitchCategory(m_currentCategory);
                MarkDirty(false, L"已删除已录入人脸");
            } catch (const std::exception&) {
                MarkDirty(false, L"删除人脸模板失败");
            }
        }
        return;
    }

    // Footer buttons
    if (id == SettingID::BTN_RESET) {
        ResetToDefaults();
        return;
    }
    if (id == SettingID::BTN_SAVE) {
        SaveSettings();
        MarkDirty(false, L"设置已保存");
        RebuildFooterControls();
        return;
    }
    if (id == SettingID::BTN_APPLY) {
        ApplySettings();
        MarkDirty(false, L"已保存并应用");
        RebuildFooterControls();
        return;
    }
    if (id == SettingID::CLAUDE_INSTALL) {
        ClaudeHookActionResult result = ClaudeHookInstaller::Install();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, result.message);
        return;
    }
    if (id == SettingID::CLAUDE_REINSTALL) {
        ClaudeHookActionResult result = ClaudeHookInstaller::Reinstall();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, result.message);
        return;
    }
    if (id == SettingID::CLAUDE_UNINSTALL) {
        ClaudeHookActionResult result = ClaudeHookInstaller::Uninstall();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, result.message);
        return;
    }
    if (id == SettingID::CODEX_INSTALL) {
        CodexHookActionResult result = CodexHookInstaller::Install();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, result.message);
        return;
    }
    if (id == SettingID::CODEX_REINSTALL) {
        CodexHookActionResult result = CodexHookInstaller::Reinstall();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, result.message);
        return;
    }
    if (id == SettingID::CODEX_UNINSTALL) {
        CodexHookActionResult result = CodexHookInstaller::Uninstall();
        SwitchCategory(m_currentCategory);
        MarkDirty(false, result.message);
        return;
    }
    // TextInput: toggle edit
    if (ctrl && ctrl->kind == ControlKind::TextInput) {
        SetFocusedControl(id);
    }
}

void SettingsWindow::SetFocusedControl(int id) {
    // 取消旧焦点
    if (m_activeTextInputId && m_activeTextInputId != id) {
        SettingsControl* old = FindControlById(m_activeControls, m_activeTextInputId);
        if (old) {
            old->editing = false;
            SyncStateFromControl(m_activeTextInputId);
        }
    }
    m_activeTextInputId = id;
    m_focusedControlId  = id;
    if (id) {
        SettingsControl* ctrl = FindControlById(m_activeControls, id);
        if (ctrl) {
            ctrl->editing = true;
            ctrl->cursorPos = int(ctrl->inputText.size());
        }
        m_caretBlinkStart = GetTickCount64();
        SetFocus(m_hwnd);
    }
}

void SettingsWindow::FocusNextControl(bool reverse) {
    // 简单跳至下一个可聚焦控件
    int curId = m_focusedControlId;
    bool found = (curId == 0);
    for (int pass = 0; pass < 2; ++pass) {
        auto range = m_activeControls;
        if (reverse) std::reverse(range.begin(), range.end());
        for (auto& c : range) {
            if (c.kind != ControlKind::TextInput && c.kind != ControlKind::Toggle) continue;
            if (found) { SetFocusedControl(c.id); return; }
            if (c.id == curId) found = true;
        }
        found = true; // 第二遍从头开始
    }
}

void SettingsWindow::UpdateBoundTextValue(int id, const std::wstring& val) {
    SettingsControl* ctrl = FindControlById(m_activeControls, id);
    if (ctrl) { 
        ctrl->inputText = val; 
        ctrl->cursorPos = int(val.size()); 
        
        if (id == SettingID::WEATHER_CITY) {
            m_citySearchText = val;  // 持久化，防止页面重建时丢失
            UpdateCityFilter(val);
        }
    }
}

bool SettingsWindow::IsControlFocusable(const SettingsControl& c) const {
    return c.enabled && (c.kind == ControlKind::Toggle ||
                         c.kind == ControlKind::TextInput ||
                         c.kind == ControlKind::Button);
}

// ====================================================================
// 渲染入口
// ====================================================================
void SettingsWindow::Render() {
    if (!m_renderTarget || !m_solidBrush) return;
    if (m_renderTarget->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    DrawWindowBackground();
    DrawTitleBar();
    DrawSidebar();
    DrawHeader();

    float scrollY = m_scrollSpring.GetValue();
    D2D1_RECT_F clip = D2D1::RectF(CONT_L - 1, CONT_T, WIN_W - CONT_PAD + 1, CONT_B);
    m_renderTarget->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    if (!m_outgoingControls.empty()) {
        DrawPageLayer(m_outgoingControls,
                      m_contentOutAlpha.GetValue(),
                      m_contentOutOffsetY.GetValue(), scrollY);
    }
    DrawPageLayer(m_activeControls,
                  m_contentInAlpha.GetValue(),
                  m_contentInOffsetY.GetValue(), scrollY);

    m_renderTarget->PopAxisAlignedClip();
    DrawFooter();

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        EnsureDeviceResources();
    }
}

// ====================================================================
// 窗口背景
// ====================================================================
void SettingsWindow::DrawWindowBackground() {
    m_renderTarget->Clear(m_windowColor);
}

// ====================================================================
// 标题栏
// ====================================================================
void SettingsWindow::DrawTitleBar() {
    FillRoundedRect(D2D1::RectF(0, 0, WIN_W, TITLEBAR), 16.f, m_sidebarColor);

    struct TrafficLightMeta {
        D2D1_COLOR_F color;
        const wchar_t* symbol;
    };
    const TrafficLightMeta dots[] = {
        { CF(1.f, 0.373f, 0.341f), L"\u00D7" },
        { CF(1.f, 0.741f, 0.180f), L"\u2212" },
        { CF(0.153f, 0.788f, 0.247f), L"\uFF0B" },
    };

    for (int i = 0; i < 3; ++i) {
        float hover = m_trafficLightHoverSprings[i].GetValue();
        D2D1_RECT_F rect = GetTrafficLightRect(i);
        D2D1_ELLIPSE dot = D2D1::Ellipse(
            D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f),
            TRAFFIC_LIGHT_RADIUS, TRAFFIC_LIGHT_RADIUS);
        FillEllipse(dot, LerpCF(dots[i].color, CF(1.f, 1.f, 1.f), hover * 0.08f));

        if (hover > 0.01f) {
            DrawTextBlock(dots[i].symbol, m_captionFormat.Get(),
                D2D1::RectF(rect.left, rect.top - 1.f, rect.right, rect.bottom + 1.f),
                CF(0.20f, 0.16f, 0.16f), hover * 0.92f,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    DrawTextBlock(L"设置", m_captionFormat.Get(),
        D2D1::RectF(76.f, 0.f, 200.f, TITLEBAR), m_secondaryTextColor, 1.f,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RECT_F closeRect = GetCloseButtonRect();
    float closeHover = m_closeButtonHover.GetValue();
    D2D1_COLOR_F closeFill = LerpCF(CF(1.0f, 0.37f, 0.34f), CF(0.90f, 0.20f, 0.18f), closeHover * 0.55f);
    D2D1_COLOR_F closeStroke = m_darkMode ? CF(0.47f, 0.10f, 0.10f) : CF(0.73f, 0.22f, 0.20f);
    D2D1_ELLIPSE closeEllipse = D2D1::Ellipse(
        D2D1::Point2F((closeRect.left + closeRect.right) * 0.5f, (closeRect.top + closeRect.bottom) * 0.5f),
        (closeRect.right - closeRect.left) * 0.5f,
        (closeRect.bottom - closeRect.top) * 0.5f);
    FillEllipse(closeEllipse, closeFill);
    m_solidBrush->SetColor(closeStroke);
    m_solidBrush->SetOpacity(0.9f);
    m_renderTarget->DrawEllipse(closeEllipse, m_solidBrush.Get(), 1.0f);
    m_solidBrush->SetOpacity(1.0f);
    DrawTextBlock(L"\u00D7", m_captionFormat.Get(), closeRect,
        CF(1.f, 1.f, 1.f), closeHover > 0.01f ? 0.96f : 0.92f,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ====================================================================
// 侧边栏
// ====================================================================
void SettingsWindow::DrawSidebar() {
    FillRoundedRect(D2D1::RectF(0, 0, SIDEBAR, WIN_H), 16.f, m_sidebarColor);
    m_solidBrush->SetColor(m_hairlineColor);
    m_solidBrush->SetOpacity(1.f);
    m_renderTarget->DrawLine(D2D1::Point2F(SIDEBAR, 14.f), D2D1::Point2F(SIDEBAR, WIN_H - 14.f), m_solidBrush.Get(), 1.f);

    DrawTextBlock(L"偏好设置", m_captionFormat.Get(),
        D2D1::RectF(NAV_X + 4.f, 50.f, SIDEBAR - NAV_X, 70.f), m_secondaryTextColor, 1.f,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float indY = m_sidebarSelectionY.GetValue();
    FillRoundedRect(D2D1::RectF(NAV_X, indY, NAV_X + NAV_W, indY + NAV_H), 10.f, m_sidebarSelectionColor);

    for (int i = 0; i < kCatCount; ++i) {
        float ny = NAV_Y0 + i * (NAV_H + NAV_GAP);
        float hov = m_navHoverSprings[i].GetValue();
        DrawNavItem(i, kCats[i].icon, kCats[i].label, ny,
            m_currentCategory == SettingCategory(i), hov);
    }
}

void SettingsWindow::DrawNavItem(int idx, const wchar_t* icon, const wchar_t* label,
                                 float ny, bool active, float hoverAlpha) {
    if (!active && hoverAlpha > 0.01f) {
        D2D1_COLOR_F hc = m_darkMode ? CF(1.f, 1.f, 1.f, 0.05f * hoverAlpha) : CF(0.f, 0.f, 0.f, 0.04f * hoverAlpha);
        FillRoundedRect(D2D1::RectF(NAV_X, ny, NAV_X + NAV_W, ny + NAV_H), 10.f, hc);
    }
    D2D1_COLOR_F tc = active ? m_accentColor : m_textColor;
    DrawTextBlock(icon, m_iconFormat.Get(),
        D2D1::RectF(NAV_X + 2.f, ny, NAV_X + 26.f, ny + NAV_H), tc, 1.f,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextBlock(label, m_bodyFormat.Get(),
        D2D1::RectF(NAV_X + 28.f, ny, NAV_X + NAV_W - 4.f, ny + NAV_H), tc, 1.f,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ====================================================================
// 页眉
// ====================================================================
void SettingsWindow::DrawHeader() {
    int ci = int(m_currentCategory);
    DrawTextBlock(kCats[ci].title, m_titleFormat.Get(),
        D2D1::RectF(CONT_L, TITLEBAR + 10.f, WIN_W - 20.f, TITLEBAR + 42.f),
        m_textColor, 1.f);
    DrawTextBlock(kCats[ci].subtitle, m_bodyFormat.Get(),
        D2D1::RectF(CONT_L, TITLEBAR + 44.f, WIN_W - 20.f, TITLEBAR + 66.f),
        m_secondaryTextColor, 1.f);
    m_solidBrush->SetColor(m_hairlineColor);
    m_solidBrush->SetOpacity(1.f);
    m_renderTarget->DrawLine(D2D1::Point2F(CONT_L, CONT_T - 2.f), D2D1::Point2F(WIN_W - 20.f, CONT_T - 2.f), m_solidBrush.Get(), 1.f);

    float sa = m_statusAlpha.GetValue();
    if (sa > 0.01f && !m_statusText.empty()) {
        float pw = 180.f;
        float ph = 26.f;
        float px = WIN_W - 20.f - pw;
        float py = TITLEBAR + (HEADER - ph) / 2.f;
        D2D1_COLOR_F pillBg = m_darkMode ? CF(0.22f, 0.22f, 0.27f, sa) : CF(0.88f, 0.92f, 1.f, sa);
        FillRoundedRect(D2D1::RectF(px, py, px + pw, py + ph), 13.f, pillBg);
        DrawTextBlock(m_statusText, m_captionFormat.Get(),
            D2D1::RectF(px + 8.f, py, px + pw - 8.f, py + ph), m_secondaryTextColor, sa,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

// ====================================================================
// 底部
// ====================================================================
void SettingsWindow::DrawFooter() {
    bool isAbout = (m_currentCategory == SettingCategory::About || m_currentCategory == SettingCategory::Agent);
    FillRoundedRect(D2D1::RectF(SIDEBAR, CONT_B, WIN_W, WIN_H), 0.f, m_footerColor);
    m_solidBrush->SetColor(m_hairlineColor);
    m_solidBrush->SetOpacity(1.f);
    m_renderTarget->DrawLine(D2D1::Point2F(CONT_L, CONT_B), D2D1::Point2F(WIN_W - 20.f, CONT_B), m_solidBrush.Get(), 1.f);
    if (isAbout) return;

    for (const auto& c : m_footerControls) {
        D2D1_RECT_F sr = D2D1::RectF(
            CONT_L + c.bounds.left, CONT_B + c.bounds.top,
            CONT_L + c.bounds.right, CONT_B + c.bounds.bottom);
        DrawButtonAtRect(c, sr, 1.f);
    }
}

// ====================================================================
// 页面层
// ====================================================================
void SettingsWindow::DrawPageLayer(const std::vector<SettingsControl>& ctrls,
                                   float alpha, float offsetY, float scrollY) {
    if (alpha < 0.01f) return;
    for (const auto& c : ctrls) {
        if (c.kind != ControlKind::Card) continue;
        D2D1_RECT_F sr = D2D1::RectF(
            CONT_L + c.bounds.left, CONT_T + c.bounds.top - scrollY + offsetY,
            CONT_L + c.bounds.right, CONT_T + c.bounds.bottom - scrollY + offsetY);
        FillRoundedRect(sr, 12.f, WithAlpha(m_cardColor, alpha));
        DrawRoundedRect(sr, 12.f, WithAlpha(m_cardStrokeColor, alpha * 0.9f), 1.f);
    }
    for (const auto& c : ctrls) {
        if (c.kind == ControlKind::Card) continue;
        DrawControl(c, alpha, offsetY, scrollY);
    }

    // 搜索结果下拉面板 (仅在天气页激活且有过滤结果时)
    if (m_currentCategory == SettingCategory::Weather && m_citySearchActive && !m_filteredCities.empty()) {
        const SettingsControl* cityInput = FindControlById(ctrls, SettingID::WEATHER_CITY);
        if (cityInput) {
            float x = CONT_L + cityInput->bounds.left;
            float inputBottom = CONT_T + cityInput->bounds.bottom - scrollY + offsetY;
            float w = cityInput->bounds.right - cityInput->bounds.left;
            float h = float(m_filteredCities.size()) * 40.f;

            float panelTop = (inputBottom + 2 + h > WIN_H - 8.f)
                ? CONT_T + cityInput->bounds.top - scrollY + offsetY - h - 2
                : inputBottom + 2;

            D2D1_RECT_F panelRect = D2D1::RectF(x, panelTop, x + w, panelTop + h);
            FillRoundedRect(panelRect, 8.f, m_cardColor, alpha);
            DrawRoundedRect(panelRect, 8.f, m_cardStrokeColor, 1.f, alpha);

            for (size_t i = 0; i < m_filteredCities.size(); ++i) {
                const auto* city = m_filteredCities[i];
                D2D1_RECT_F itemRect = D2D1::RectF(x, panelTop + i * 40.f, x + w, panelTop + (i + 1) * 40.f);

                // 分割线（非第一项）
                if (i > 0) {
                    m_solidBrush->SetColor(WithAlpha(m_cardStrokeColor, alpha));
                    m_solidBrush->SetOpacity(1.f);
                    m_renderTarget->DrawLine(
                        D2D1::Point2F(itemRect.left + 12, itemRect.top),
                        D2D1::Point2F(itemRect.right - 12, itemRect.top),
                        m_solidBrush.Get(), 0.5f);
                }

                std::wstring label = city->nameZH;
                std::wstring sublabel = city->adm1 + L" · " + city->adm2;
                DrawTextBlock(label, m_bodyFormat.Get(),
                    D2D1::RectF(itemRect.left + 12, itemRect.top + 4, itemRect.right - 12, itemRect.top + 24),
                    m_textColor, alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                DrawTextBlock(sublabel, m_captionFormat.Get(),
                    D2D1::RectF(itemRect.left + 12, itemRect.top + 23, itemRect.right - 12, itemRect.bottom - 4),
                    m_secondaryTextColor, alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }
        }
    }

    // 联动选择面板（省 → 地级市）
    if (m_currentCategory == SettingCategory::Weather && m_regionPickerOpen) {
        const SettingsControl* cityInput = FindControlById(ctrls, SettingID::WEATHER_CITY);
        if (cityInput) {
            constexpr int COLS = 4;
            float itemW  = CONT_W / COLS;
            float itemH  = 34.f;
            float headerH = 32.f;   // 标题 / 返回按钮行高
            float panelLeft = CONT_L + cityInput->bounds.left;
            float ibBottom  = CONT_T + cityInput->bounds.bottom - scrollY + offsetY;

            if (m_regionPickerLevel == 0) {
                // ── Level 0: 省份 4 列网格 ──
                size_t total = m_allRegions.size() + 1; // +1 "全部"
                size_t rows  = (total + COLS - 1) / COLS;
                float panelH = headerH + float(rows) * itemH + 4.f;
                float panelTop = (ibBottom + 4 + panelH > WIN_H - 8.f)
                    ? CONT_T + cityInput->bounds.top - scrollY + offsetY - panelH - 4
                    : ibBottom + 4;

                D2D1_RECT_F panelRect = D2D1::RectF(panelLeft, panelTop, panelLeft + CONT_W, panelTop + panelH);
                FillRoundedRect(panelRect, 8.f, m_cardColor, alpha);
                DrawRoundedRect(panelRect, 8.f, m_cardStrokeColor, 1.f, alpha);

                // 标题行
                DrawTextBlock(L"选择省份", m_bodyFormat.Get(),
                    D2D1::RectF(panelLeft + 14, panelTop, panelLeft + CONT_W, panelTop + headerH),
                    m_secondaryTextColor, alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                m_solidBrush->SetColor(WithAlpha(m_cardStrokeColor, alpha));
                m_solidBrush->SetOpacity(1.f);
                m_renderTarget->DrawLine({panelLeft + 8, panelTop + headerH},
                    {panelLeft + CONT_W - 8, panelTop + headerH}, m_solidBrush.Get(), 0.5f);

                for (size_t i = 0; i < total; ++i) {
                    bool isAll  = (i == 0);
                    const std::wstring& lbl = isAll ? L"全部" : m_allRegions[i - 1];
                    bool sel = isAll ? m_selectedRegion.empty() : (m_allRegions[i - 1] == m_selectedRegion);
                    float ix = panelLeft + float(i % COLS) * itemW;
                    float iy = panelTop + headerH + 2.f + float(i / COLS) * itemH;
                    D2D1_RECT_F ir = D2D1::RectF(ix, iy, ix + itemW, iy + itemH);
                    if (sel) FillRoundedRect({ir.left+3,ir.top+2,ir.right-3,ir.bottom-2}, 5.f, WithAlpha(m_accentColor, alpha*0.18f), alpha);
                    DrawTextBlock(lbl, m_bodyFormat.Get(), {ir.left+4,ir.top,ir.right-4,ir.bottom},
                        sel ? WithAlpha(m_accentColor, alpha) : WithAlpha(m_textColor, alpha),
                        alpha, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                }
            } else {
                // ── Level 1: 所选省份内的地级市列表 ──
                // 收集去重地级市
                std::vector<std::wstring> prefectures;
                for (const auto& c : m_allCities) {
                    if (c.adm1 != m_selectedRegion) continue;
                    if (std::find(prefectures.begin(), prefectures.end(), c.adm2) == prefectures.end())
                        prefectures.push_back(c.adm2);
                }
                size_t total = prefectures.size() + 1; // +1 "全部（该省）"
                size_t rows  = (total + COLS - 1) / COLS;
                float panelH = headerH + float(rows) * itemH + 4.f;
                float panelTop = (ibBottom + 4 + panelH > WIN_H - 8.f)
                    ? CONT_T + cityInput->bounds.top - scrollY + offsetY - panelH - 4
                    : ibBottom + 4;

                D2D1_RECT_F panelRect = D2D1::RectF(panelLeft, panelTop, panelLeft + CONT_W, panelTop + panelH);
                FillRoundedRect(panelRect, 8.f, m_cardColor, alpha);
                DrawRoundedRect(panelRect, 8.f, m_cardStrokeColor, 1.f, alpha);

                // 标题行：← 返回  +  省份名
                DrawTextBlock(L"← " + m_selectedRegion, m_bodyFormat.Get(),
                    D2D1::RectF(panelLeft + 14, panelTop, panelLeft + CONT_W, panelTop + headerH),
                    m_accentColor, alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                m_solidBrush->SetColor(WithAlpha(m_cardStrokeColor, alpha));
                m_solidBrush->SetOpacity(1.f);
                m_renderTarget->DrawLine({panelLeft + 8, panelTop + headerH},
                    {panelLeft + CONT_W - 8, panelTop + headerH}, m_solidBrush.Get(), 0.5f);

                for (size_t i = 0; i < total; ++i) {
                    bool isAll  = (i == 0);
                    const std::wstring& lbl = isAll ? L"全省" : prefectures[i - 1];
                    bool sel = isAll ? m_selectedPrefecture.empty() : (prefectures[i - 1] == m_selectedPrefecture);
                    float ix = panelLeft + float(i % COLS) * itemW;
                    float iy = panelTop + headerH + 2.f + float(i / COLS) * itemH;
                    D2D1_RECT_F ir = D2D1::RectF(ix, iy, ix + itemW, iy + itemH);
                    if (sel) FillRoundedRect({ir.left+3,ir.top+2,ir.right-3,ir.bottom-2}, 5.f, WithAlpha(m_accentColor, alpha*0.18f), alpha);
                    DrawTextBlock(lbl, m_bodyFormat.Get(), {ir.left+4,ir.top,ir.right-4,ir.bottom},
                        sel ? WithAlpha(m_accentColor, alpha) : WithAlpha(m_textColor, alpha),
                        alpha, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                }
            }
        }
    }
}

D2D1_COLOR_F SettingsWindow::WithAlpha(D2D1_COLOR_F c, float a) const {
    return CF(c.r, c.g, c.b, c.a * a);
}

// ====================================================================
// 控件分发
// ====================================================================
void SettingsWindow::DrawControl(const SettingsControl& c,
                                 float alpha, float offsetY, float scrollY) {
    D2D1_RECT_F sr = D2D1::RectF(
        CONT_L + c.bounds.left, CONT_T + c.bounds.top - scrollY + offsetY,
        CONT_L + c.bounds.right, CONT_T + c.bounds.bottom - scrollY + offsetY);
    switch (c.kind) {
    case ControlKind::Toggle:    DrawToggleControl(c, sr, alpha); break;
    case ControlKind::Slider:    DrawSliderControl(c, sr, alpha); break;
    case ControlKind::Button:    DrawButtonAtRect(c, sr, alpha); break;
    case ControlKind::TextInput: DrawTextInputCtrl(c, sr, alpha); break;
    case ControlKind::Separator: DrawSeparatorLine(c, sr, alpha); break;
    case ControlKind::Label:
    case ControlKind::SubLabel:  DrawLabelRow(c, sr, alpha); break;
    default: break;
    }
}

void SettingsWindow::DrawToggleControl(const SettingsControl& c,
                                       const D2D1_RECT_F& sr, float alpha) {
    const float visualAlpha = c.enabled ? alpha : alpha * 0.45f;
    float cx = sr.left + 16.f;
    float midY = (sr.top + sr.bottom) / 2.f;
    DrawTextBlock(c.text, m_bodyFormat.Get(),
        D2D1::RectF(cx, midY - 18.f, sr.right - 80.f, midY - 2.f),
        m_textColor, visualAlpha);
    DrawTextBlock(c.subtitle, m_captionFormat.Get(),
        D2D1::RectF(cx, midY - 1.f, sr.right - 80.f, midY + 14.f),
        m_secondaryTextColor, visualAlpha);

    float tw = 52.f;
    float th = 28.f;
    float tx = sr.right - 16.f - tw;
    float ty = midY - th / 2.f;
    float t = c.animatedValue;
    D2D1_COLOR_F offCol = m_darkMode ? CF(0.31f, 0.31f, 0.35f) : CF(0.84f, 0.84f, 0.87f);
    D2D1_COLOR_F trackCol = LerpCF(offCol, m_accentColor, t);
    FillRoundedRect(D2D1::RectF(tx, ty, tx + tw, ty + th), th / 2.f, WithAlpha(trackCol, visualAlpha));
    float knobX = tx + 2.f + t * (tw - 26.f);
    FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX + 12.f, ty + 14.f), 12.f, 12.f), WithAlpha(CF(1.f, 1.f, 1.f), visualAlpha));
}

// ====================================================================
// Slider
// ====================================================================
void SettingsWindow::DrawSliderControl(const SettingsControl& c,
                                       const D2D1_RECT_F& sr, float alpha) {
    float cx = sr.left + 16.f;
    float t = c.animatedValue;
    float raw = c.minVal + t * (c.maxVal - c.minVal);

    DrawTextBlock(c.text, m_bodyFormat.Get(),
        D2D1::RectF(cx, sr.top + 10.f, sr.right - 80.f, sr.top + 27.f), m_textColor, alpha);
    DrawTextBlock(c.subtitle, m_captionFormat.Get(),
        D2D1::RectF(cx, sr.top + 28.f, sr.right - 80.f, sr.top + 43.f), m_secondaryTextColor, alpha);

    wchar_t vb[32];
    swprintf_s(vb, L"%d%s", int(raw), c.suffix.c_str());
    DrawTextBlock(vb, m_captionFormat.Get(),
        D2D1::RectF(sr.right - 70.f, sr.top + 10.f, sr.right - 12.f, sr.top + 30.f),
        m_accentColor, alpha,
        DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    float trackY = sr.top + 52.f;
    float trackL = cx;
    float trackR = cx + TRACK_W;
    D2D1_COLOR_F bgCol = m_darkMode ? CF(0.3f, 0.3f, 0.34f) : CF(0.82f, 0.82f, 0.86f);
    FillRoundedRect(D2D1::RectF(trackL, trackY, trackR, trackY + TRACK_H), TRACK_H / 2.f, WithAlpha(bgCol, alpha));
    float fillR = trackL + t * TRACK_W;
    if (fillR > trackL) {
        FillRoundedRect(D2D1::RectF(trackL, trackY, fillR, trackY + TRACK_H), TRACK_H / 2.f, WithAlpha(m_accentColor, alpha));
    }

    float hov = c.hoverSpring.GetValue();
    float kr = KNOB_R + hov * 1.5f;
    float kx = trackL + t * TRACK_W;
    float ky = trackY + TRACK_H / 2.f;
    FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ky + 1.f), kr, kr), WithAlpha(CF(0.f, 0.f, 0.f, 0.10f), alpha));
    FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ky), kr, kr), WithAlpha(CF(1.f, 1.f, 1.f), alpha));
    DrawRoundedRect(D2D1::RectF(kx - kr, ky - kr, kx + kr, ky + kr), kr, WithAlpha(m_cardStrokeColor, alpha * 0.5f), 1.f);
}

// ====================================================================
// TextInput
// ====================================================================
void SettingsWindow::DrawTextInputCtrl(const SettingsControl& c,
                                        const D2D1_RECT_F& sr, float alpha) {
    float cx = sr.left + 16.f;
    DrawTextBlock(c.text, m_bodyFormat.Get(),
        D2D1::RectF(cx, sr.top + 10.f, sr.right - 16.f, sr.top + 27.f), m_textColor, alpha);
    DrawTextBlock(c.subtitle, m_captionFormat.Get(),
        D2D1::RectF(cx, sr.top + 28.f, sr.right - 16.f, sr.top + 43.f), m_secondaryTextColor, alpha);

    float iby = sr.top + 48.f;
    float ibh = 30.f;
    D2D1_RECT_F ib = D2D1::RectF(cx, iby, sr.right - 16.f, iby + ibh);
    FillRoundedRect(ib, 7.f, WithAlpha(m_inputFillColor, alpha));
    D2D1_COLOR_F strokeCol = c.editing ? WithAlpha(m_accentColor, alpha)
                                       : WithAlpha(m_cardStrokeColor, alpha);
    DrawRoundedRect(ib, 7.f, strokeCol, c.editing ? 1.5f : 1.f);

    // WEATHER_CITY: 右侧绘制地区筛选胶囊
    bool isCitySearch = (c.id == SettingID::WEATHER_CITY);
    if (isCitySearch) {
        D2D1_RECT_F pill = GetRegionPillRect(ib);
        bool regionActive = !m_selectedRegion.empty() || !m_selectedPrefecture.empty();
        D2D1_COLOR_F pillFill = regionActive ? WithAlpha(m_accentColor, alpha * 0.85f)
                                             : WithAlpha(m_inputFillColor, alpha);
        D2D1_COLOR_F pillStroke = regionActive ? WithAlpha(m_accentColor, alpha)
                                              : WithAlpha(m_cardStrokeColor, alpha);
        D2D1_COLOR_F pillText = regionActive ? WithAlpha(CF(1.f, 1.f, 1.f), alpha)
                                            : WithAlpha(m_secondaryTextColor, alpha);
        FillRoundedRect(pill, 5.f, pillFill, alpha);
        DrawRoundedRect(pill, 5.f, pillStroke, 1.f, alpha);
        // 显示：地级市 > 省份 > 全部
        std::wstring regionLabel;
        if (!m_selectedPrefecture.empty())      regionLabel = m_selectedPrefecture;
        else if (!m_selectedRegion.empty())     regionLabel = m_selectedRegion;
        else                                    regionLabel = L"全部";
        regionLabel += L" ▾";
        m_solidBrush->SetColor(pillText);
        m_solidBrush->SetOpacity(1.f);
        ComPtr<IDWriteTextLayout> pil;
        m_dwriteFactory->CreateTextLayout(regionLabel.c_str(), (UINT32)regionLabel.size(),
            m_captionFormat.Get(), pill.right - pill.left - 4, pill.bottom - pill.top, &pil);
        if (pil) {
            pil->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pil->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_renderTarget->DrawTextLayout(D2D1::Point2F(pill.left + 2.f, pill.top), pil.Get(), m_solidBrush.Get());
        }
    }

    D2D1_RECT_F tr = isCitySearch ? GetCitySearchTextRect(ib)
                                  : D2D1::RectF(ib.left + 8.f, ib.top, ib.right - 8.f, ib.bottom);

    // 占位提示：无内容时显示灰色提示
    if (c.inputText.empty() && c.editing) {
        DrawTextBlock(c.subtitle.empty() ? L"" : c.subtitle,
            m_bodyFormat.Get(), tr, WithAlpha(m_secondaryTextColor, alpha * 0.5f),
            alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        DrawTextBlock(c.inputText,
            m_bodyFormat.Get(), tr, m_textColor, alpha,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (c.editing) {
        bool blink = ((GetTickCount64() - m_caretBlinkStart) % 1000) < 500;
        if (blink) {
            float caretX = tr.left;
            if (!c.inputText.empty()) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                m_dwriteFactory->CreateTextLayout(c.inputText.c_str(), (UINT32)c.inputText.length(),
                    m_bodyFormat.Get(), tr.right - tr.left, tr.bottom - tr.top, &layout);
                if (layout) {
                    DWRITE_HIT_TEST_METRICS metrics;
                    float tx, ty;
                    layout->HitTestTextPosition((UINT32)c.cursorPos, FALSE, &tx, &ty, &metrics);
                    caretX += tx;
                }
            }
            caretX = (std::min)(caretX, tr.right - 2.f);
            m_solidBrush->SetColor(m_textColor);
            m_solidBrush->SetOpacity(alpha);
            m_renderTarget->DrawLine(D2D1::Point2F(caretX, ib.top + 6.f), D2D1::Point2F(caretX, ib.bottom - 6.f), m_solidBrush.Get(), 1.5f);
            m_solidBrush->SetOpacity(1.f);
        }
    }
}
// ====================================================================
// Button
// ====================================================================
void SettingsWindow::DrawButtonAtRect(const SettingsControl& c,
                                      const D2D1_RECT_F& sr, float alpha) {
    float hov = c.hoverSpring.GetValue();
    bool dis = !c.enabled;
    D2D1_COLOR_F fill;
    D2D1_COLOR_F stroke;
    D2D1_COLOR_F tc;
    if (dis) {
        fill = m_darkMode ? CF(0.18f, 0.18f, 0.21f) : CF(0.91f, 0.91f, 0.94f);
        stroke = m_cardStrokeColor;
        tc = m_secondaryTextColor;
    } else if (c.isPrimary) {
        fill = LerpCF(m_accentColor, CF(0.4f, 0.6f, 1.f), hov * 0.2f);
        stroke = fill;
        tc = CF(1.f, 1.f, 1.f);
    } else {
        D2D1_COLOR_F base = m_darkMode ? CF(0.22f, 0.22f, 0.26f) : CF(0.95f, 0.96f, 0.98f);
        fill = LerpCF(base, m_darkMode ? CF(0.27f, 0.27f, 0.32f) : CF(0.88f, 0.91f, 1.f), hov * 0.5f);
        stroke = m_cardStrokeColor;
        tc = m_textColor;
    }
    FillRoundedRect(sr, 10.f, WithAlpha(fill, alpha));
    DrawRoundedRect(sr, 10.f, WithAlpha(stroke, alpha * 0.6f), 1.f);
    DrawTextBlock(c.text, m_bodyFormat.Get(), sr, WithAlpha(tc, alpha), 1.f,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void SettingsWindow::DrawSeparatorLine(const SettingsControl& c,
                                       const D2D1_RECT_F& sr, float alpha) {
    m_solidBrush->SetColor(WithAlpha(m_hairlineColor, alpha));
    m_solidBrush->SetOpacity(1.f);
    m_renderTarget->DrawLine(D2D1::Point2F(sr.left, sr.top), D2D1::Point2F(sr.right, sr.top), m_solidBrush.Get(), 1.f);
}

void SettingsWindow::DrawLabelRow(const SettingsControl& c,
                                  const D2D1_RECT_F& sr, float alpha) {
    DrawTextBlock(c.text,
        c.kind == ControlKind::SubLabel ? m_captionFormat.Get() : m_bodyFormat.Get(),
        D2D1::RectF(sr.left, sr.top, sr.right, sr.bottom),
        c.kind == ControlKind::SubLabel ? m_secondaryTextColor : m_textColor,
        alpha, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void SettingsWindow::DrawFocusRing(const D2D1_RECT_F& r, float radius, float alpha) {
    DrawRoundedRect(r, radius, WithAlpha(m_accentColor, alpha), 2.f);
}

// ====================================================================
// D2D 辅助
// ====================================================================
void SettingsWindow::FillRoundedRect(const D2D1_RECT_F& r, float radius,
                                     const D2D1_COLOR_F& col, float alpha) {
    if (col.a * alpha < 0.005f) return;
    m_solidBrush->SetColor(col);
    m_solidBrush->SetOpacity(alpha);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), m_solidBrush.Get());
    m_solidBrush->SetOpacity(1.f);
}

void SettingsWindow::DrawRoundedRect(const D2D1_RECT_F& r, float radius,
                                     const D2D1_COLOR_F& col, float strokeW, float alpha) {
    if (col.a * alpha < 0.005f) return;
    m_solidBrush->SetColor(col);
    m_solidBrush->SetOpacity(alpha);
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), m_solidBrush.Get(), strokeW);
    m_solidBrush->SetOpacity(1.f);
}

void SettingsWindow::FillEllipse(const D2D1_ELLIPSE& e, const D2D1_COLOR_F& col, float alpha) {
    if (col.a * alpha < 0.005f) return;
    m_solidBrush->SetColor(col);
    m_solidBrush->SetOpacity(alpha);
    m_renderTarget->FillEllipse(e, m_solidBrush.Get());
    m_solidBrush->SetOpacity(1.f);
}

void SettingsWindow::DrawTextBlock(const std::wstring& text, IDWriteTextFormat* fmt,
                                   const D2D1_RECT_F& r, const D2D1_COLOR_F& col,
                                   float alpha, D2D1_DRAW_TEXT_OPTIONS opts) {
    if (text.empty() || !fmt || alpha < 0.005f) return;
    m_solidBrush->SetColor(col);
    m_solidBrush->SetOpacity(alpha);
    m_renderTarget->DrawTextW(text.c_str(), UINT32(text.size()), fmt, r, m_solidBrush.Get(), opts);
    m_solidBrush->SetOpacity(1.f);
}

void SettingsWindow::DrawTextBlock(const std::wstring& text, IDWriteTextFormat* fmt,
                                   const D2D1_RECT_F& r, const D2D1_COLOR_F& col, float alpha,
                                   DWRITE_TEXT_ALIGNMENT ha, DWRITE_PARAGRAPH_ALIGNMENT va) {
    if (text.empty() || !fmt || alpha < 0.005f) return;
    DWRITE_TEXT_ALIGNMENT ph = fmt->GetTextAlignment();
    DWRITE_PARAGRAPH_ALIGNMENT pv = fmt->GetParagraphAlignment();
    fmt->SetTextAlignment(ha);
    fmt->SetParagraphAlignment(va);
    m_solidBrush->SetColor(col);
    m_solidBrush->SetOpacity(alpha);
    m_renderTarget->DrawTextW(text.c_str(), UINT32(text.size()), fmt, r, m_solidBrush.Get());
    m_solidBrush->SetOpacity(1.f);
    fmt->SetTextAlignment(ph);
    fmt->SetParagraphAlignment(pv);
}

// ====================================================================
// 布局辅助
// ====================================================================
D2D1_RECT_F SettingsWindow::GetWindowRectDip() const { return D2D1::RectF(0.f, 0.f, WIN_W, WIN_H); }
D2D1_RECT_F SettingsWindow::GetSidebarRect() const { return D2D1::RectF(0.f, 0.f, SIDEBAR, WIN_H); }

// 将 IME 候选窗口定位到当前活动输入框的正下方
void SettingsWindow::PositionIMEWindow() {
    if (!m_hwnd || !m_activeTextInputId) return;
    HIMC himc = ImmGetContext(m_hwnd);
    if (!himc) return;

    SettingsControl* ctrl = FindControlById(m_activeControls, m_activeTextInputId);
    if (ctrl) {
        float scrollY = m_scrollSpring.GetValue();
        float ibx = CONT_L + ctrl->bounds.left + 16.f;
        float iby = CONT_T + ctrl->bounds.top + 48.f - scrollY;  // 输入框内顶部

        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = DipToPixels(ibx);
        cf.ptCurrentPos.y = DipToPixels(iby + 30.f);  // 输入框底部
        ImmSetCompositionWindow(himc, &cf);
    }
    ImmReleaseContext(m_hwnd, himc);
}

// 地区筛选按钮：输入框右侧小胶囊
D2D1_RECT_F SettingsWindow::GetRegionPillRect(const D2D1_RECT_F& ib) const {
    constexpr float pillW = 72.f;
    return D2D1::RectF(ib.right - pillW - 4.f, ib.top + 4.f, ib.right - 4.f, ib.bottom - 4.f);
}

// 搜索文字区域：把输入框右侧留给地区按钮
D2D1_RECT_F SettingsWindow::GetCitySearchTextRect(const D2D1_RECT_F& ib) const {
    constexpr float pillW = 72.f;
    return D2D1::RectF(ib.left + 8.f, ib.top, ib.right - pillW - 12.f, ib.bottom);
}
D2D1_RECT_F SettingsWindow::GetHeaderRect() const { return D2D1::RectF(CONT_L, TITLEBAR, WIN_W, CONT_T); }
D2D1_RECT_F SettingsWindow::GetContentViewportRect() const { return D2D1::RectF(CONT_L, CONT_T, WIN_W - CONT_PAD, CONT_B); }
D2D1_RECT_F SettingsWindow::GetFooterRect() const { return D2D1::RectF(SIDEBAR, CONT_B, WIN_W, WIN_H); }

D2D1_RECT_F SettingsWindow::GetNavigationItemRect(size_t i) const {
    float y = NAV_Y0 + float(i) * (NAV_H + NAV_GAP);
    return D2D1::RectF(NAV_X, y, NAV_X + NAV_W, y + NAV_H);
}

D2D1_RECT_F SettingsWindow::GetTrafficLightRect(int i) const {
    float centerX = TRAFFIC_LIGHT_START_X + i * TRAFFIC_LIGHT_SPACING;
    float centerY = TITLEBAR * 0.5f;
    float hitRadius = TRAFFIC_LIGHT_RADIUS + 1.f;
    return D2D1::RectF(centerX - hitRadius, centerY - hitRadius, centerX + hitRadius, centerY + hitRadius);
}

void SettingsWindow::SetStatusText(const std::wstring& text) {
    m_statusText = text;
    m_statusAlpha.SetTarget(1.f);
    StartAnimationTimer();
}

void SettingsWindow::ApplyRoundedRegion() const {
    if (!m_hwnd) return;
    RECT clientRect{};
    GetClientRect(m_hwnd, &clientRect);
    int radius = DipToPixels(WINDOW_CORNER_RADIUS);
    HRGN rgn = CreateRoundRectRgn(0, 0, clientRect.right + 1, clientRect.bottom + 1, radius, radius);
    SetWindowRgn(m_hwnd, rgn, TRUE);
}

void SettingsWindow::AutoLocateCity() {
    if (m_isLocating) return;
    m_isLocating = true;
    m_statusText = L"正在定位中...";
    m_statusAlpha.SetTarget(1.f);
    StartAnimationTimer();

    std::wstring apiKey = m_weatherApiKey;
    if (apiKey.empty()) apiKey = L"bf25bfa431394152adb2f4ed57ac092e";

    HWND hwnd = m_hwnd;
    std::thread([this, hwnd, apiKey]() {
        auto HttpGet = [](const wchar_t* host, const wchar_t* path) -> std::string {
            std::string response;
            HINTERNET hSession = WinHttpOpen(L"DynamicIsland/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (hSession) {
                HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
                if (hConnect) {
                    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                    if (hRequest) {
                        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                            if (WinHttpReceiveResponse(hRequest, nullptr)) {
                                DWORD size = 0;
                                do {
                                    if (!WinHttpQueryDataAvailable(hRequest, &size)) break;
                                    if (size == 0) break;
                                    std::string buf(size, '\0');
                                    DWORD read = 0;
                                    if (WinHttpReadData(hRequest, &buf[0], size, &read)) {
                                        buf.resize(read);
                                        response += buf;
                                    }
                                } while (size > 0);
                            }
                        }
                        WinHttpCloseHandle(hRequest);
                    }
                    WinHttpCloseHandle(hConnect);
                }
                WinHttpCloseHandle(hSession);
            }
            return response;
        };

        char path[512];
        int len = WideCharToMultiByte(CP_UTF8, 0, apiKey.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string akUtf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, apiKey.c_str(), -1, &akUtf8[0], len, nullptr, nullptr);
        snprintf(path, sizeof(path), "/v2/city/lookup?location=auto_ip&key=%s", akUtf8.c_str());
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        std::wstring wpath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen);

        std::string resp = HttpGet(L"geoapi.qweather.com", wpath.c_str());
        
        LocationResult* res = new LocationResult();

        if (!resp.empty()) {
            auto extractField = [](const std::string& json, const char* key) -> std::string {
                std::string pattern = "\""; pattern += key; pattern += "\":\"";
                size_t p = json.find(pattern);
                if (p == std::string::npos) return "";
                p += pattern.length();
                size_t end = json.find("\"", p);
                if (end == std::string::npos) return "";
                return json.substr(p, end - p);
            };

            std::string code = extractField(resp, "code");
            if (code == "200") {
                std::string name = extractField(resp, "name");
                std::string id = extractField(resp, "id");
                
                int nlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
                res->name.resize(nlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &res->name[0], nlen);

                int ilen = MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, nullptr, 0);
                res->id.resize(ilen - 1);
                MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, &res->id[0], ilen);
                
                res->success = true;
            }
        }

        PostMessage(hwnd, WM_LOCATION_RESULT, res->success ? 1 : 0, reinterpret_cast<LPARAM>(res));
    }).detach();
}

// ====================================================================
// WndProc
// ====================================================================
LRESULT CALLBACK SettingsWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<SettingsWindow*>(reinterpret_cast<LPCREATESTRUCT>(lp)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    return self ? self->WndProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            float wx = this->PixelsToDipX(pt.x);
            float wy = this->PixelsToDipY(pt.y);
            D2D1_RECT_F closeRect = GetCloseButtonRect();
            bool onCloseButton = wx >= closeRect.left && wx <= closeRect.right &&
                                 wy >= closeRect.top && wy <= closeRect.bottom;
            bool onTrafficLights = HitTestTrafficLight(wx, wy) >= 0;
            if (wy <= TITLEBAR && wx > 70.f && !onCloseButton && !onTrafficLights) return HTCAPTION;
        }
        return hit;
    }

    case WM_ERASEBKGND:
        return 1;
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (EnsureDeviceResources()) Render();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LOCATION_RESULT: {
        LocationResult* res = reinterpret_cast<LocationResult*>(lp);
        m_isLocating = false;
        if (wp && res) {
            m_weatherCity = res->name;
            m_weatherLocationId = res->id;
            m_citySearchActive = false;
            m_filteredCities.clear();
            UpdateBoundTextValue(SettingID::WEATHER_CITY, m_weatherCity);
            SetFocusedControl(0);
            m_citySearchText.clear();
            ApplySettings();  // 写入 config.ini 并通知主窗口刷新天气
            MarkDirty(false, L"已定位并应用: " + res->name);
            RefreshControlTargets();
        } else {
            SetStatusText(L"定位失败，请手动选择城市");
        }
        delete res;
        return 0;
    }

    case WM_SIZE:
        ResizeRenderTarget(LOWORD(lp), HIWORD(lp));
        ApplyRoundedRegion();
        return 0;

    case WM_DPICHANGED: {
        UINT dpiX = LOWORD(wp);
        if (dpiX == 0) dpiX = 96;
        m_dpiScale = float(dpiX) / 96.f;
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        DiscardDeviceResources();
        EnsureDeviceResources();
        ApplyRoundedRegion();
        return 0;
    }

    case WM_TIMER:
        if (wp == ANIM_TIMER) {
            ULONGLONG now = GetTickCount64();
            float dt = (m_lastAnimationTick == 0) ? 0.016f : float(now - m_lastAnimationTick) / 1000.0f;
            dt = std::clamp(dt, 0.008f, 0.033f);
            m_lastAnimationTick = now;
            TickAnimations(dt);
            float wa = std::clamp(m_windowAlpha.GetValue(), 0.f, 1.f);
            SetLayeredWindowAttributes(hwnd, 0, BYTE(wa * 255), LWA_ALPHA);
            if (!HasActiveAnimations()) {
                KillTimer(hwnd, ANIM_TIMER);
                m_lastAnimationTick = 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE: {
        if (!m_mouseTracking) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            m_mouseTracking = true;
        }
        float wx = this->PixelsToDipX(GET_X_LPARAM(lp));
        float wy = this->PixelsToDipY(GET_Y_LPARAM(lp));
        UpdateHoverState(wx, wy);
        if (m_draggingSliderId) {
            UpdateSliderDrag(wx);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_mouseTracking = false;
        m_hoveredNavIndex = -1;
        m_hoveredTrafficLight = -1;
        m_hoveredCloseButton = false;
        m_closeButtonHover.SetTarget(0.f);
        for (int i = 0; i < kCatCount; ++i) m_navHoverSprings[i].SetTarget(0.f);
        for (auto& spring : m_trafficLightHoverSprings) spring.SetTarget(0.f);
        for (auto& c : m_activeControls) { c.hovered = false; c.hoverSpring.SetTarget(0.f); }
        for (auto& c : m_footerControls) { c.hovered = false; c.hoverSpring.SetTarget(0.f); }
        StartAnimationTimer();
        return 0;

    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        float wx = this->PixelsToDipX(GET_X_LPARAM(lp));
        float wy = this->PixelsToDipY(GET_Y_LPARAM(lp));

        // 城市下拉选择逻辑
        if (m_currentCategory == SettingCategory::Weather && m_citySearchActive && !m_filteredCities.empty()) {
            const SettingsControl* cityInput = FindControlById(m_activeControls, SettingID::WEATHER_CITY);
            if (cityInput) {
                float x = CONT_L + cityInput->bounds.left;
                float scrollY = m_scrollSpring.GetValue();
                float inputBottom = CONT_T + cityInput->bounds.bottom - scrollY;
                float w = cityInput->bounds.right - cityInput->bounds.left;
                float h = float(m_filteredCities.size()) * 40.f;

                // 与绘制逻辑保持一致：不足时向上弹出
                float panelTop;
                if (inputBottom + 2 + h > WIN_H - 8.f) {
                    panelTop = CONT_T + cityInput->bounds.top - scrollY - h - 2;
                } else {
                    panelTop = inputBottom + 2;
                }

                for (size_t i = 0; i < m_filteredCities.size(); ++i) {
                    D2D1_RECT_F itemRect = D2D1::RectF(x, panelTop + i * 40.f, x + w, panelTop + (i + 1) * 40.f);
                    if (wx >= itemRect.left && wx <= itemRect.right && wy >= itemRect.top && wy <= itemRect.bottom) {
                        const auto* city = m_filteredCities[i];
                        const auto buildSelectionLabel = [](const auto& selectedCity) {
                            std::wstring area = TrimWeatherAreaSuffix(selectedCity.adm2);
                            if (!area.empty() && !selectedCity.nameZH.empty() &&
                                area != selectedCity.nameZH && selectedCity.adm2 != selectedCity.nameZH) {
                                return area + L" " + selectedCity.nameZH;
                            }
                            return selectedCity.nameZH.empty() ? area : selectedCity.nameZH;
                        };
                        m_weatherCity = buildSelectionLabel(*city);
                        m_weatherLocationId = city->id;
                        UpdateBoundTextValue(SettingID::WEATHER_CITY, m_weatherCity);
                        m_citySearchActive = false;
                        m_filteredCities.clear();
                        SetFocusedControl(0);
                        m_citySearchText.clear();
                        ApplySettings();  // 写入 config.ini 并通知主窗口刷新天气
                        MarkDirty(false, L"已应用: " + m_weatherCity);
                        RefreshControlTargets();
                        return 0;
                    }
                }
            }
        }

        // 联动选择面板命中
        if (m_currentCategory == SettingCategory::Weather && m_regionPickerOpen) {
            const SettingsControl* cityInput = FindControlById(m_activeControls, SettingID::WEATHER_CITY);
            if (cityInput) {
                float scrollY  = m_scrollSpring.GetValue();
                constexpr int COLS = 4;
                float itemW    = CONT_W / COLS;
                float itemH    = 34.f;
                float headerH  = 32.f;
                float panelLeft = CONT_L + cityInput->bounds.left;
                float ibBottom  = CONT_T + cityInput->bounds.bottom - scrollY;

                // 重建和绘制一样的列表
                std::vector<std::wstring> items;
                if (m_regionPickerLevel == 0) {
                    items.push_back(L"");  // 全部，空字符串占位
                    for (auto& r : m_allRegions) items.push_back(r);
                } else {
                    items.push_back(L"");  // 全省，空字符串占位
                    for (const auto& c : m_allCities) {
                        if (c.adm1 != m_selectedRegion) continue;
                        if (std::find(items.begin(), items.end(), c.adm2) == items.end())
                            items.push_back(c.adm2);
                    }
                }

                size_t total  = items.size();
                size_t rows   = (total + COLS - 1) / COLS;
                float panelH  = headerH + float(rows) * itemH + 4.f;
                float panelTop = (ibBottom + 4 + panelH > WIN_H - 8.f)
                    ? CONT_T + cityInput->bounds.top - scrollY - panelH - 4
                    : ibBottom + 4;

                bool inPanel = wx >= panelLeft && wx <= panelLeft + CONT_W &&
                               wy >= panelTop  && wy <= panelTop  + panelH;
                if (inPanel) {
                    // 点击标题行 → level1 时返回 level0
                    if (wy <= panelTop + headerH) {
                        if (m_regionPickerLevel == 1) {
                            m_regionPickerLevel = 0;
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                        return 0;
                    }
                    // 点击格子
                    float relX = wx - panelLeft;
                    float relY = wy - (panelTop + headerH + 2.f);
                    if (relY >= 0) {
                        size_t col = size_t(relX / itemW);
                        size_t row = size_t(relY / itemH);
                        size_t idx = row * COLS + col;
                        if (idx < total) {
                            if (m_regionPickerLevel == 0) {
                                if (idx == 0) {
                                    // "全部"：清空省份和地级市，直接关闭
                                    m_selectedRegion.clear();
                                    m_selectedPrefecture.clear();
                                    m_regionPickerOpen = false;
                                } else {
                                    m_selectedRegion = items[idx];
                                    m_selectedPrefecture.clear();
                                    m_regionPickerLevel = 1; // 进入地级市选择
                                    InvalidateRect(hwnd, nullptr, FALSE);
                                    return 0;
                                }
                            } else {
                                // level 1: 选地级市
                                m_selectedPrefecture = (idx == 0) ? L"" : items[idx];
                                m_regionPickerOpen = false;
                            }
                        }
                    }
                    m_citySearchActive = true;
                    UpdateCityFilter(m_citySearchText);
                    SettingsControl* ci = FindControlById(m_activeControls, SettingID::WEATHER_CITY);
                    if (ci) { ci->editing = true; m_caretBlinkStart = GetTickCount64(); }
                    SetFocusedControl(SettingID::WEATHER_CITY);
                    PositionIMEWindow();
                    BuildPage(m_currentCategory, m_activeControls, m_activeContentHeight);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                // 点在面板外：关闭
                m_regionPickerOpen = false;
                m_regionPickerLevel = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }

        // 地区胶囊按钮命中（在城市搜索框控件范围内的右侧 pill 区域）
        if (m_currentCategory == SettingCategory::Weather) {
            const SettingsControl* cityInput = FindControlById(m_activeControls, SettingID::WEATHER_CITY);
            if (cityInput) {
                float scrollY = m_scrollSpring.GetValue();
                float iby  = CONT_T + cityInput->bounds.top + 48.f - scrollY;
                float ibx  = CONT_L + cityInput->bounds.left + 16.f;
                float ibr  = CONT_L + cityInput->bounds.right - 16.f;
                D2D1_RECT_F ib = D2D1::RectF(ibx, iby, ibr, iby + 30.f);
                D2D1_RECT_F pill = GetRegionPillRect(ib);
                if (wx >= pill.left && wx <= pill.right && wy >= pill.top && wy <= pill.bottom) {
                    m_regionPickerOpen = !m_regionPickerOpen;
                    m_regionPickerLevel = 0;  // 每次从省级开始
                    m_filteredCities.clear();
                    StartAnimationTimer();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        }

        D2D1_RECT_F closeRect = GetCloseButtonRect();
        if (wx >= closeRect.left && wx <= closeRect.right &&
            wy >= closeRect.top && wy <= closeRect.bottom) { Hide(); return 0; }
        if (HitTestTrafficLight(wx, wy) == 0) { Hide(); return 0; }
        int nav = HitTestNavigation(wx, wy);
        if (nav >= 0) {
            if (nav != int(m_currentCategory)) {
                m_citySearchActive = false;
                m_citySearchText.clear();
                m_filteredCities.clear();
                m_regionPickerOpen = false;
                SwitchCategory(SettingCategory(nav));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        SettingsControl* ctrl = HitTestInteractiveControl(wx, wy);
        if (ctrl) {
            m_pressedControlId = ctrl->id;
            if (ctrl->kind == ControlKind::Slider) BeginSliderDrag(ctrl->id, wx);
        }
        for (auto& c : m_footerControls) {
            float fx = wx - CONT_L;
            float fy = wy - CONT_B;
            if (c.enabled && fx >= c.bounds.left && fx <= c.bounds.right && fy >= c.bounds.top && fy <= c.bounds.bottom) {
                HandleControlActivation(c.id);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        float wx = this->PixelsToDipX(GET_X_LPARAM(lp));
        float wy = this->PixelsToDipY(GET_Y_LPARAM(lp));
        if (m_draggingSliderId) {
            EndSliderDrag();
        } else if (m_pressedControlId) {
            SettingsControl* ctrl = HitTestInteractiveControl(wx, wy);
            if (ctrl && ctrl->id == m_pressedControlId) HandleControlActivation(ctrl->id);
        }
        m_pressedControlId = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        float delta = -float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 40.f;
        m_scrollSpring.SetTarget(ClampScroll(m_scrollSpring.GetValue() + delta));
        StartAnimationTimer();
        return 0;
    }

    case WM_IME_CHAR:
        // 拦截：不转发给 DefWindowProc，防止 IME 上屏字符再次经由 WM_CHAR 插入
        return 0;

    case WM_IME_COMPOSITION: {
        // 处理中文 IME 上屏结果
        if (lp & GCS_RESULTSTR) {
            HIMC himc = ImmGetContext(hwnd);
            if (himc && m_activeTextInputId) {
                LONG byteLen = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                if (byteLen > 0) {
                    std::wstring result(byteLen / sizeof(wchar_t), L'\0');
                    ImmGetCompositionStringW(himc, GCS_RESULTSTR, &result[0], byteLen);
                    SettingsControl* ctrl = FindControlById(m_activeControls, m_activeTextInputId);
                    if (ctrl && ctrl->editing) {
                        ctrl->inputText.insert(ctrl->cursorPos, result);
                        ctrl->cursorPos += int(result.size());
                        m_caretBlinkStart = GetTickCount64();
                        SyncStateFromControl(m_activeTextInputId);
                        PositionIMEWindow();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                ImmReleaseContext(hwnd, himc);
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_IME_SETCONTEXT:
        // 允许系统显示组合串窗口（输入中间状态），但隐藏默认候选窗口
        lp &= ~ISC_SHOWUICOMPOSITIONWINDOW;
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_CHAR: {
        wchar_t ch = wchar_t(wp);
        if (m_activeTextInputId) {
            SettingsControl* ctrl = FindControlById(m_activeControls, m_activeTextInputId);
            if (ctrl && ctrl->editing) {
                if (ch == L'\b') {
                    if (ctrl->cursorPos > 0) {
                        ctrl->inputText.erase(ctrl->cursorPos - 1, 1);
                        --ctrl->cursorPos;
                    }
                } else if (ch >= 0x20 && ch != 0x7F) {
                    ctrl->inputText.insert(ctrl->cursorPos, 1, ch);
                    ++ctrl->cursorPos;
                }
                m_caretBlinkStart = GetTickCount64();
                SyncStateFromControl(m_activeTextInputId);
                PositionIMEWindow();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_PASTE: {
        if (m_activeTextInputId) {
            SettingsControl* ctrl = FindControlById(m_activeControls, m_activeTextInputId);
            if (ctrl && ctrl->editing) {
                const std::wstring clipboardText = ReadClipboardUnicodeText(hwnd);
                if (!clipboardText.empty()) {
                    ctrl->inputText.insert(ctrl->cursorPos, clipboardText);
                    ctrl->cursorPos += int(clipboardText.size());
                    m_caretBlinkStart = GetTickCount64();
                    SyncStateFromControl(m_activeTextInputId);
                    PositionIMEWindow();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
        }
        break;
    }

    case WM_KEYDOWN:
        if (m_activeTextInputId) {
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if ((ctrlPressed && wp == 'V') || (shiftPressed && wp == VK_INSERT)) {
                SettingsControl* ctrl = FindControlById(m_activeControls, m_activeTextInputId);
                if (ctrl && ctrl->editing) {
                    const std::wstring clipboardText = ReadClipboardUnicodeText(hwnd);
                    if (!clipboardText.empty()) {
                        ctrl->inputText.insert(ctrl->cursorPos, clipboardText);
                        ctrl->cursorPos += int(clipboardText.size());
                        m_caretBlinkStart = GetTickCount64();
                        SyncStateFromControl(m_activeTextInputId);
                        PositionIMEWindow();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
            }
        }
        switch (wp) {
        case VK_ESCAPE:
            if (m_activeTextInputId) SetFocusedControl(0);
            else Hide();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case VK_RETURN:
            if (m_isDirty) {
                ApplySettings();
                MarkDirty(false, L"已保存并应用");
                RebuildFooterControls();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case VK_TAB:
            FocusNextControl((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case VK_LEFT:
        case VK_RIGHT:
            if (m_activeTextInputId) {
                SettingsControl* ctrl = FindControlById(m_activeControls, m_activeTextInputId);
                if (ctrl) {
                    if (wp == VK_LEFT && ctrl->cursorPos > 0) --ctrl->cursorPos;
                    if (wp == VK_RIGHT && ctrl->cursorPos < int(ctrl->inputText.size())) ++ctrl->cursorPos;
                    m_caretBlinkStart = GetTickCount64();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        break;

    case WM_SETTINGCHANGE:
        if (m_followSystemTheme) {
            m_darkMode = GetSystemDarkMode();
            UpdateTheme();
            SwitchCategory(m_currentCategory);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && m_activeTextInputId) SetFocusedControl(0);
        return 0;

    case WM_CLOSE:
        Hide();
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        m_visible = false;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
