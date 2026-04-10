#pragma once
#include "IIslandComponent.h"
#include "Messages.h"
#include <string>
#include <vector>
#include <unordered_map>

class AlertComponent : public IIslandComponent {
public:
    void OnAttach(SharedResources* res) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_isAlertActive; }

    void SetAlertState(bool active, const AlertInfo& info);
    bool LoadAlertIcon(const std::wstring& file);
    bool LoadAlertIconFromMemory(const std::vector<uint8_t>& data);
    void ClearAlertBitmap() { m_alertBitmap.Reset(); }

private:
    const wchar_t* GetIconText(int alertType);
    ID2D1SolidColorBrush* GetBrush(int alertType);

    struct TextLayoutCacheEntry {
        ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
        float maxWidth;
    };
    ComPtr<IDWriteTextLayout> GetOrCreateTextLayout(
        const std::wstring& text, IDWriteTextFormat* format, float maxWidth, const std::wstring& cacheKey);

    void RenderCompact(const D2D1_RECT_F& rect, float contentAlpha);
    void RenderExpanded(const D2D1_RECT_F& rect, float contentAlpha);

    SharedResources* m_res = nullptr;
    bool m_isAlertActive = false;
    AlertInfo m_alert;
    ComPtr<ID2D1Bitmap> m_alertBitmap;
    std::unordered_map<std::wstring, TextLayoutCacheEntry> m_textLayoutCache;

    static constexpr float ICON_SIZE = 24.0f;
    static constexpr float ART_SIZE = 60.0f;
    static constexpr float COMPACT_THRESHOLD = 60.0f;
};