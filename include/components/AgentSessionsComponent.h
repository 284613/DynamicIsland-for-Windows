#pragma once

#include "AgentSessionModel.h"
#include "Constants.h"
#include "IIslandComponent.h"
#include "IslandState.h"
#include <functional>
#include <vector>

class AgentSessionsComponent : public IIslandComponent {
public:
    AgentSessionsComponent();

    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override;
    bool NeedsRender() const override;
    bool OnMouseWheel(float x, float y, int delta) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseClick(float x, float y) override;

    void SetDarkMode(bool darkMode) { m_darkMode = darkMode; }
    void SetDisplayMode(IslandDisplayMode mode) { m_mode = mode; }
    void SetSessions(const std::vector<AgentSessionSummary>& summaries) { m_summaries = summaries; }
    void SetFilter(AgentSessionFilter filter) { m_filter = filter; }
    void SetSelectedSession(AgentKind kind, const std::wstring& sessionId, const std::vector<AgentHistoryEntry>& history);
    void SetCompactState(AgentKind provider, bool chooserOpen);
    bool ShouldShowWorkingEdgeBadge(IslandDisplayMode mode, float islandHeight) const;
    void DrawWorkingEdgeBadge(const D2D1_RECT_F& rect, float alpha);

    void SetOnRequestOpenExpanded(std::function<void()> callback) { m_onRequestOpenExpanded = std::move(callback); }
    void SetOnRequestCloseExpanded(std::function<void()> callback) { m_onRequestCloseExpanded = std::move(callback); }
    void SetOnFilterChanged(std::function<void(AgentSessionFilter)> callback) { m_onFilterChanged = std::move(callback); }
    void SetOnSessionSelected(std::function<void(AgentKind, const std::wstring&)> callback) { m_onSessionSelected = std::move(callback); }
    void SetOnCompactProviderChanged(std::function<void(AgentKind)> callback) { m_onCompactProviderChanged = std::move(callback); }
    void SetOnCompactChooserOpenChanged(std::function<void(bool)> callback) { m_onCompactChooserOpenChanged = std::move(callback); }
    void SetOnApprovePermission(std::function<void(const std::wstring&)> callback) { m_onApprovePermission = std::move(callback); }
    void SetOnDenyPermission(std::function<void(const std::wstring&)> callback) { m_onDenyPermission = std::move(callback); }

private:
    enum class ClaudeVisualPhase {
        Idle,
        Processing,
        WaitingForApproval,
        WaitingForInput
    };

    enum class HitKind {
        None,
        CompactPet,
        CompactExpandBody,
        CompactChooseClaude,
        CompactChooseCodex,
        CompactDismissChooser,
        FilterClaude,
        FilterCodex,
        SessionRow,
        ApprovePermission,
        DenyPermission
    };

    struct HitTarget {
        HitKind kind = HitKind::None;
        AgentKind sessionKind = AgentKind::Claude;
        std::wstring sessionId;
        D2D1_RECT_F rect = D2D1::RectF(0, 0, 0, 0);
    };

    struct OverviewSection {
        std::wstring title;
        std::wstring body;
        D2D1_COLOR_F accent = D2D1::ColorF(1, 1, 1, 1);
    };

    void DrawCompact(const D2D1_RECT_F& rect, float alpha);
    void DrawExpanded(const D2D1_RECT_F& rect, float alpha);
    void DrawClaudeCompactVisual(const D2D1_RECT_F& rect, float alpha, const AgentSessionSummary& summary, ClaudeVisualPhase phase);
    void DrawCodexCompactVisual(const D2D1_RECT_F& rect, float alpha, const AgentSessionSummary& summary);
    void DrawCompactProviderChooser(const D2D1_RECT_F& rect, float alpha, const std::vector<AgentKind>& providers);
    void DrawClaudeCrabIcon(const D2D1_RECT_F& rect, float alpha, bool animateLegs);
    void DrawCodexPetIcon(const D2D1_RECT_F& rect, float alpha, bool animate);
    void DrawClaudeCompactStatus(ClaudeVisualPhase phase, const D2D1_RECT_F& rect, float alpha);
    void DrawClaudeStatusIcon(ClaudeVisualPhase phase, const D2D1_RECT_F& rect, float alpha);
    void DrawProcessingSpinner(const D2D1_RECT_F& rect, float alpha);
    void DrawCompactHud(const AgentSessionSummary& summary, const D2D1_RECT_F& rect, float alpha);
    void DrawSessionRow(const AgentSessionSummary& summary, const D2D1_RECT_F& rect, float alpha, bool selected, bool hovered);
    float DrawOverviewSection(const OverviewSection& section, const D2D1_RECT_F& rect, float alpha);
    void DrawPill(const D2D1_RECT_F& rect, const std::wstring& text, bool selected, float alpha, bool hovered);
    void DrawSourceGlyph(AgentKind kind, const D2D1_RECT_F& rect, float alpha);
    void DrawPixelIcon(const D2D1_RECT_F& rect,
        const D2D1_COLOR_F& color,
        const D2D1_POINT_2F* solidPixels,
        size_t solidCount,
        const D2D1_POINT_2F* fadedPixels,
        size_t fadedCount,
        float alpha) const;
    void DrawTextLine(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        const D2D1_COLOR_F& color, float alpha, bool wrap, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING) const;
    float MeasureWrappedHeight(const std::wstring& text, IDWriteTextFormat* format, float width) const;
    std::vector<const AgentSessionSummary*> FilteredSessions() const;
    const AgentSessionSummary* FindSelectedSummary() const;
    const AgentSessionSummary* FindSummary(AgentKind kind) const;
    std::vector<OverviewSection> BuildOverviewForSelectedSession() const;
    const AgentHistoryEntry* FindLatestEntry(AgentEntryDirection direction) const;
    const AgentHistoryEntry* FindLatestOutputEntry() const;
    const AgentHistoryEntry* FindLatestToolEntry() const;
    std::vector<AgentKind> AvailableCompactProviders() const;
    bool HasAnimatedProcessingProvider() const;
    AgentKind ResolveCompactProvider() const;
    AgentKind ResolveWorkingBadgeProvider() const;
    static std::wstring ShortSessionId(const std::wstring& sessionId);
    ClaudeVisualPhase GetClaudePrimaryPhase() const;
    ClaudeVisualPhase PhaseForSummary(const AgentSessionSummary& summary) const;
    static bool Contains(const D2D1_RECT_F& rect, float x, float y);
    static float Clamp(float value, float minValue, float maxValue);
    static std::wstring RelativeTimeLabel(uint64_t timestampMs);
    static std::wstring ProjectLabel(const std::wstring& projectPath);
    D2D1_COLOR_F SurfaceFill() const;
    D2D1_COLOR_F CardFill() const;
    D2D1_COLOR_F StrokeColor() const;
    D2D1_COLOR_F AccentColor(AgentKind kind) const;
    D2D1_COLOR_F PhaseColor(ClaudeVisualPhase phase) const;
    HitTarget HitTest(float x, float y) const;

    SharedResources* m_res = nullptr;
    ComPtr<IDWriteTextFormat> m_headingFormat;
    ComPtr<IDWriteTextFormat> m_bodyFormat;
    ComPtr<IDWriteTextFormat> m_smallFormat;
    ComPtr<IDWriteTextFormat> m_tinyFormat;
    ComPtr<IDWriteTextFormat> m_symbolFormat;

    IslandDisplayMode m_mode = IslandDisplayMode::Idle;
    bool m_darkMode = true;
    std::vector<AgentSessionSummary> m_summaries;
    AgentSessionFilter m_filter = AgentSessionFilter::Claude;
    AgentKind m_selectedKind = AgentKind::Claude;
    AgentKind m_compactProvider = AgentKind::Claude;
    bool m_compactChooserOpen = false;
    std::wstring m_selectedSessionId;
    std::vector<AgentHistoryEntry> m_selectedHistory;

    D2D1_RECT_F m_lastRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_surfaceRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_listCardRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_historyCardRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_listViewportRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_historyViewportRect = D2D1::RectF(0, 0, 0, 0);
    std::vector<HitTarget> m_hits;
    float m_sessionListScroll = 0.0f;
    float m_sessionListMaxScroll = 0.0f;
    float m_historyScroll = 0.0f;
    float m_historyMaxScroll = 0.0f;
    float m_animationTime = 0.0f;
    HitKind m_hoveredKind = HitKind::None;
    AgentKind m_hoveredSessionKind = AgentKind::Claude;
    std::wstring m_hoveredSessionId;

    std::function<void()> m_onRequestOpenExpanded;
    std::function<void()> m_onRequestCloseExpanded;
    std::function<void(AgentSessionFilter)> m_onFilterChanged;
    std::function<void(AgentKind, const std::wstring&)> m_onSessionSelected;
    std::function<void(AgentKind)> m_onCompactProviderChanged;
    std::function<void(bool)> m_onCompactChooserOpenChanged;
    std::function<void(const std::wstring&)> m_onApprovePermission;
    std::function<void(const std::wstring&)> m_onDenyPermission;
};
