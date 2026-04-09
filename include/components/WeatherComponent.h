#pragma once
#include "IIslandComponent.h"
#include "IslandState.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

// 天气类型枚举（原 RenderEngine::WeatherType）
enum class WeatherType {
    Clear,
    PartlyCloudy,
    Cloudy,
    Rainy,
    Thunder,
    Snow,
    Fog,
    Default
};

class WeatherComponent : public IIslandComponent {
public:
    WeatherComponent() = default;
    ~WeatherComponent() = default;

    // ── IIslandComponent ──────────────────────────────────────────────
    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_isExpanded; }
    bool NeedsRender() const override { return m_isExpanded; }
    bool OnMouseWheel(float x, float y, int delta) override;

    // ── 数据更新（由 DynamicIsland / EventBus 调用）────────────────────
    void SetWeatherData(
        const std::wstring& desc,
        float temp,
        const std::wstring& iconId,
        const std::vector<RenderContext::HourlyForecast>& hourly,
        const std::vector<RenderContext::DailyForecast>& daily);

    void SetExpanded(bool expanded);
    void SetViewMode(WeatherViewMode mode) { m_viewMode = mode; }
    WeatherViewMode GetViewMode() const    { return m_viewMode; }

    // compact 模式：在岛右侧绘制小天气图标+温度（供 RenderEngine 过渡期调用）
    void DrawCompact(float iconX, float iconY, float iconSize,
                     float contentAlpha, ULONGLONG currentTimeMs);

    // 触发一次性入场动画重置（天气类型切换时）
    void ResetAnimation() { m_animPhase = 0.0f; m_lastAnimTime = 0; }

private:
    // 子绘制函数
    void DrawWeatherExpanded(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime);
    void DrawWeatherDaily(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTime);
    void DrawWeatherAmbientBg(float L, float T, float R, float B, float alpha, ULONGLONG currentTime);
    void DrawWeatherIcon(float x, float y, float size, float alpha, ULONGLONG currentTime);
    WeatherType MapWeatherDescToType(const std::wstring& desc) const;

    // 辅助绘制
    void DrawCloud(float cx, float cy, float s, float op, ID2D1Brush* brush);
    void DrawLine(float x1, float y1, float x2, float y2, ID2D1Brush* brush, float w);

    ComPtr<IDWriteTextLayout> GetOrCreateTextLayout(
        const std::wstring& text, IDWriteTextFormat* fmt,
        float maxWidth, const std::wstring& cacheKey);

    // ── 共享资源指针（不拥有） ──────────────────────────────────────────
    SharedResources* m_res = nullptr;

    // ── 天气数据 ────────────────────────────────────────────────────────
    std::wstring m_desc;
    float        m_temp      = 0.0f;
    std::wstring m_iconId;
    std::vector<RenderContext::HourlyForecast> m_hourly;
    std::vector<RenderContext::DailyForecast>  m_daily;

    // ── 组件状态 ────────────────────────────────────────────────────────
    bool            m_isExpanded = false;
    WeatherViewMode m_viewMode   = WeatherViewMode::Hourly;
    WeatherType     m_weatherType = WeatherType::Default;
    float           m_animPhase   = 0.0f;
    ULONGLONG       m_lastAnimTime = 0;

    // TextLayout 缓存
    struct CacheEntry {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        float maxWidth = 0.0f;
    };
    std::unordered_map<std::wstring, CacheEntry> m_layoutCache;
};
