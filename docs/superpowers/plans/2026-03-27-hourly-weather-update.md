# Hourly Weather Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update weather every 15 minutes based on actual local time, showing current hour's weather instead of only updating on hour changes.

**Architecture:** Modify the weather update logic in `WeatherPlugin` (refresh interval) and `SystemMonitor` (update trigger) to use 15-minute intervals based on local system time. The wttr.in API already returns real-time current conditions.

**Tech Stack:** C++ WinHTTP, Windows API for time

---

## Task 1: Change WeatherPlugin refresh interval to 15 minutes

**Files:**
- Modify: `WeatherPlugin.cpp:78-84`

- [ ] **Step 1: Read current WeatherPlugin.cpp to verify Update method**

```cpp
void WeatherPlugin::Update(float deltaTime) {
    size_t now = GetTickCount64();
    if (now - m_lastUpdateTime < 10 * 60 * 1000 && m_lastUpdateTime != 0) {
        return;
    }
    FetchWeather();
}
```

- [ ] **Step 2: Change 10-minute interval to 15-minute interval**

```cpp
void WeatherPlugin::Update(float deltaTime) {
    size_t now = GetTickCount64();
    if (now - m_lastUpdateTime < 15 * 60 * 1000 && m_lastUpdateTime != 0) {
        return;
    }
    FetchWeather();
}
```

- [ ] **Step 3: Verify the change was applied correctly**

---

## Task 2: Change SystemMonitor update trigger to 15-minute intervals

**Files:**
- Modify: `SystemMonitor.cpp:89-103`

- [ ] **Step 1: Read current SystemMonitor.cpp UpdateWeather method**

```cpp
void SystemMonitor::UpdateWeather() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 【逻辑实现】如果是第一次更新，或者当前小时与上次更新小时不同（跨整点了）
    if (m_lastUpdateHour == -1 || st.wHour != m_lastUpdateHour) {
        if (m_weatherPlugin) {
            m_weatherPlugin->Update(0.0f); // 内部会触发 FetchWeather
            m_weatherDesc = m_weatherPlugin->GetWeatherDescription();
            m_weatherTemp = m_weatherPlugin->GetTemperature();
            m_lastUpdateHour = st.wHour;

            OutputDebugStringW(L"[SystemMonitor] 检测到小时变更，已触发天气更新。\n");
        }
    }
}
```

- [ ] **Step 2: Modify UpdateWeather to use 15-minute interval logic instead of hour-change detection**

```cpp
void SystemMonitor::UpdateWeather() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 计算当前15分钟区间: 0-3min=0, 4-7min=1, 8-11min=2, 12-15min=3,循环
    int currentQuarter = st.wMinute / 4;

    // 如果是第一次更新，或者跨了15分钟区间，就更新
    if (m_lastUpdateQuarter == -1 || currentQuarter != m_lastUpdateQuarter) {
        if (m_weatherPlugin) {
            m_weatherPlugin->Update(0.0f); // 内部会触发 FetchWeather
            m_weatherDesc = m_weatherPlugin->GetWeatherDescription();
            m_weatherTemp = m_weatherPlugin->GetTemperature();
            m_lastUpdateQuarter = currentQuarter;

            wchar_t dbg[128];
            swprintf_s(dbg, L"[SystemMonitor] 15分钟区间变更(%d)，已触发天气更新。\n", currentQuarter);
            OutputDebugStringW(dbg);
        }
    }
}
```

- [ ] **Step 3: Verify the change was applied correctly**

---

## Task 3: Update SystemMonitor header for new member variable

**Files:**
- Modify: `SystemMonitor.h:36`

- [ ] **Step 1: Read SystemMonitor.h to find the member variable declaration**

```cpp
int m_lastUpdateHour = -1; // 记录上次更新的小时
```

- [ ] **Step 2: Change member variable from hour-based to 15-minute-interval based**

```cpp
int m_lastUpdateQuarter = -1; // 记录上次更新的15分钟区间 (0-3)
```

- [ ] **Step 3: Verify the change was applied correctly**

---

## Task 4: Verify build compiles successfully

**Files:**
- Build: `DynamicIsland.vcxproj`

- [ ] **Step 1: Run build command to verify compilation**

Run: Build the project in Visual Studio or via command line
Expected: Build succeeds without errors

---

## Summary of Changes

| File | Change |
|------|--------|
| `WeatherPlugin.cpp:80` | `10 * 60 * 1000` → `15 * 60 * 1000` (10 min → 15 min) |
| `SystemMonitor.h:36` | `m_lastUpdateHour` → `m_lastUpdateQuarter` |
| `SystemMonitor.cpp:89-103` | Hour-change detection → 15-minute-interval detection |

---

## Testing Checklist

- [ ] Build succeeds
- [ ] Weather updates every 15 minutes (not just on hour change)
- [ ] Weather reflects current time's conditions (e.g., 1 AM shows night weather, not max temp)
- [ ] No memory leaks or crashes
