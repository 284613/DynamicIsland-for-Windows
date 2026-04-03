# Weather City Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make weather city configurable via config.ini instead of hardcoded "Changsha".

**Architecture:** Add a city member variable to WeatherPlugin, expose SetCity method via interface, load city from config.ini in DynamicIsland.

**Tech Stack:** C++ WinHTTP, Windows API (GetPrivateProfileStringW, WritePrivateProfileStringW)

---

## Task 1: Add SetCity method to IWeatherPlugin interface

**Files:**
- Modify: `PluginManager.h:96-103`

- [ ] **Step 1: Read current IWeatherPlugin interface**

```cpp
class IWeatherPlugin : public IPlugin {
public:
    // 获取天气描述
    virtual std::wstring GetWeatherDescription() const = 0;

    // 获取温度
    virtual float GetTemperature() const = 0;
};
```

- [ ] **Step 2: Add SetCity method to interface**

```cpp
class IWeatherPlugin : public IPlugin {
public:
    // 获取天气描述
    virtual std::wstring GetWeatherDescription() const = 0;

    // 获取温度
    virtual float GetTemperature() const = 0;

    // 设置天气城市
    virtual void SetCity(const std::wstring& city) = 0;
};
```

---

## Task 2: Add m_city member and SetCity implementation to WeatherPlugin

**Files:**
- Modify: `WeatherPlugin.h`
- Modify: `WeatherPlugin.cpp`

- [ ] **Step 1: Add m_city member variable to WeatherPlugin.h**

Add after `float m_temperature = 0.0f;`:
```cpp
std::wstring m_city = L"Changsha"; // 天气城市
```

- [ ] **Step 2: Add SetCity declaration to WeatherPlugin.h**

```cpp
void SetCity(const std::wstring& city) override;
```

- [ ] **Step 3: Implement SetCity in WeatherPlugin.cpp**

Add after the destructor:
```cpp
void WeatherPlugin::SetCity(const std::wstring& city) {
    m_city = city;
    OutputDebugStringW((L"[WeatherPlugin] City set to: " + city + L"\n").c_str());
}
```

- [ ] **Step 4: Change hardcoded "Changsha" to use m_city in FetchWeather**

Change:
```cpp
std::string myCity = "Changsha";
```

To:
```cpp
std::string myCity = GbkToUtf8(m_city.empty() ? "Changsha" : m_city);
```

---

## Task 3: Load weather city from config.ini

**Files:**
- Modify: `DynamicIsland.cpp` (LoadConfig function)

- [ ] **Step 1: Find where weather city should be loaded in LoadConfig**

In `DynamicIsland::LoadConfig()`, after loading other settings, add loading weather city:

```cpp
wchar_t weatherCityBuf[64] = { 0 };
GetPrivateProfileStringW(L"Weather", L"City", L"Changsha", weatherCityBuf, 64, configPath.c_str());
m_systemMonitor.GetWeatherPlugin()->SetCity(std::wstring(weatherCityBuf));
```

- [ ] **Step 2: Also save default city on first run**

Since WritePrivateProfileStringW only creates the key if it doesn't exist, call it once:
```cpp
WritePrivateProfileStringW(L"Weather", L"City", L"Changsha", configPath.c_str());
```

---

## Task 4: Verify build compiles successfully

**Files:**
- Build: `DynamicIsland.vcxproj`

- [ ] **Step 1: Run build command to verify compilation**

Run: Build the project
Expected: Build succeeds without errors

---

## Summary of Changes

| File | Change |
|------|--------|
| `PluginManager.h:96-103` | Add `SetCity` method to `IWeatherPlugin` interface |
| `WeatherPlugin.h` | Add `m_city` member and `SetCity` declaration |
| `WeatherPlugin.cpp` | Implement `SetCity`, use `m_city` instead of hardcoded |
| `DynamicIsland.cpp` | Load weather city from config.ini |

---

## Testing Checklist

- [ ] Build succeeds
- [ ] Weather city configurable via config.ini
- [ ] Default city is "Changsha" if not set
- [ ] Changing city in config.ini updates weather on next refresh
