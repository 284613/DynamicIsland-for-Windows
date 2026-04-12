#include "components/AgentSessionsComponent.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kExpandedCorner = 18.0f;
constexpr float kScrollStep = 34.0f;
constexpr float kClaudePromptR = 0.85f;
constexpr float kClaudePromptG = 0.47f;
constexpr float kClaudePromptB = 0.34f;

constexpr D2D1_POINT_2F kApprovalPixels[] = {
    { 7.0f, 7.0f }, { 7.0f, 11.0f },
    { 11.0f, 3.0f },
    { 15.0f, 3.0f }, { 15.0f, 19.0f }, { 15.0f, 27.0f },
    { 19.0f, 3.0f }, { 19.0f, 15.0f },
    { 23.0f, 7.0f }, { 23.0f, 11.0f }
};

constexpr D2D1_POINT_2F kReadyPixels[] = {
    { 5.0f, 15.0f }, { 9.0f, 19.0f }, { 13.0f, 23.0f },
    { 17.0f, 19.0f }, { 21.0f, 15.0f }, { 25.0f, 11.0f }, { 29.0f, 7.0f }
};

constexpr D2D1_POINT_2F kWaitingInputSolidPixels[] = {
    { 3.0f, 3.0f }, { 7.0f, 3.0f }, { 11.0f, 3.0f }, { 15.0f, 3.0f }, { 19.0f, 3.0f }, { 23.0f, 3.0f }, { 27.0f, 3.0f },
    { 3.0f, 7.0f }, { 3.0f, 11.0f }, { 3.0f, 15.0f }, { 3.0f, 19.0f }, { 3.0f, 23.0f }, { 3.0f, 27.0f },
    { 27.0f, 7.0f }, { 27.0f, 11.0f }, { 27.0f, 15.0f }, { 27.0f, 19.0f },
    { 7.0f, 23.0f },
    { 11.0f, 19.0f }, { 15.0f, 19.0f }, { 19.0f, 19.0f }, { 23.0f, 19.0f }
};

constexpr D2D1_POINT_2F kWaitingInputFadedPixels[] = {
    { 7.0f, 11.0f }, { 7.0f, 15.0f }, { 7.0f, 19.0f },
    { 11.0f, 11.0f }, { 11.0f, 15.0f },
    { 15.0f, 11.0f }, { 15.0f, 15.0f },
    { 19.0f, 15.0f }
};

constexpr D2D1_POINT_2F kRunningSolidPixels[] = {
    { 15.0f, 3.0f },
    { 7.0f, 7.0f }, { 15.0f, 7.0f }, { 23.0f, 7.0f },
    { 15.0f, 11.0f }, { 15.0f, 19.0f },
    { 3.0f, 15.0f }, { 7.0f, 15.0f }, { 11.0f, 15.0f }, { 19.0f, 15.0f }, { 23.0f, 15.0f }, { 27.0f, 15.0f },
    { 7.0f, 23.0f }, { 15.0f, 23.0f }, { 23.0f, 23.0f },
    { 15.0f, 27.0f }
};

constexpr D2D1_POINT_2F kRunningFadedPixels[] = {
    { 11.0f, 11.0f }, { 19.0f, 11.0f },
    { 11.0f, 19.0f }, { 19.0f, 19.0f }
};

constexpr D2D1_POINT_2F kIdlePixels[] = {
    { 11.0f, 15.0f }, { 15.0f, 15.0f }, { 19.0f, 15.0f }
};

constexpr const wchar_t* kSpinnerSymbols[] = {
    L"\u00B7", L"\u2722", L"\u2733", L"\u2217", L"\u273B", L"\u273D"
};

float PulseWave(float time, float speed, float minValue, float maxValue) {
    const float oscillation = 0.5f + 0.5f * std::sin(time * speed);
    return minValue + (maxValue - minValue) * oscillation;
}

std::wstring MiniBar(int usedPercent, int slots = 8) {
    if (usedPercent < 0) {
        return L"\u2014";
    }

    usedPercent = (std::max)(0, (std::min)(100, usedPercent));
    const int filled = static_cast<int>(std::round((usedPercent / 100.0) * slots));
    std::wstring bar;
    for (int i = 0; i < slots; ++i) {
        bar += (i < filled) ? L"\u2588" : L"\u2591";
    }
    return bar;
}

std::wstring RemainingPercentText(int usedPercent) {
    if (usedPercent < 0) {
        return L"n/a";
    }
    return std::to_wstring((std::max)(0, 100 - usedPercent)) + L"%";
}

std::wstring CompactCount(uint64_t value) {
    if (value >= 1000000ULL) {
        const double millions = static_cast<double>(value) / 1000000.0;
        const int tenths = static_cast<int>(std::round(millions * 10.0));
        std::wstring text = std::to_wstring(tenths / 10);
        if (tenths % 10) {
            text += L".";
            text += std::to_wstring(tenths % 10);
        }
        text += L"M";
        return text;
    }
    if (value >= 1000ULL) {
        const double thousands = static_cast<double>(value) / 1000.0;
        const int tenths = static_cast<int>(std::round(thousands * 10.0));
        std::wstring text = std::to_wstring(tenths / 10);
        if (tenths % 10) {
            text += L".";
            text += std::to_wstring(tenths % 10);
        }
        text += L"K";
        return text;
    }
    return std::to_wstring(static_cast<unsigned long long>(value));
}

D2D1_COLOR_F QuotaThresholdColor(int usedPercent) {
    if (usedPercent >= 90) {
        return D2D1::ColorF(1.0f, 0.30f, 0.30f, 1.0f);
    }
    if (usedPercent >= 70) {
        return D2D1::ColorF(1.0f, 0.70f, 0.0f, 1.0f);
    }
    return D2D1::ColorF(0.40f, 0.75f, 0.45f, 1.0f);
}

uint64_t CurrentUnixMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}

}

AgentSessionsComponent::AgentSessionsComponent() = default;

void AgentSessionsComponent::OnAttach(SharedResources* res) {
    m_res = res;
    if (!m_res || !m_res->dwriteFactory) {
        return;
    }

    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"zh-cn", &m_headingFormat);
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"zh-cn", &m_bodyFormat);
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-cn", &m_smallFormat);
    m_res->dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"zh-cn", &m_tinyFormat);
    m_res->dwriteFactory->CreateTextFormat(L"Segoe UI Symbol", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-cn", &m_symbolFormat);
}

void AgentSessionsComponent::Update(float deltaTime) {
    m_animationTime += deltaTime;
    if (m_animationTime > 1200.0f) {
        m_animationTime = std::fmod(m_animationTime, 1200.0f);
    }
}

void AgentSessionsComponent::SetSelectedSession(AgentKind kind, const std::wstring& sessionId, const std::vector<AgentHistoryEntry>& history) {
    m_selectedKind = kind;
    m_selectedSessionId = sessionId;
    m_selectedHistory = history;
    m_historyScroll = 0.0f;
}

void AgentSessionsComponent::SetCompactState(AgentKind provider, bool chooserOpen) {
    m_compactProvider = provider;
    m_compactChooserOpen = chooserOpen;
}

bool AgentSessionsComponent::IsActive() const {
    return m_mode == IslandDisplayMode::AgentCompact || m_mode == IslandDisplayMode::AgentExpanded;
}

bool AgentSessionsComponent::NeedsRender() const {
    const float lastHeight = m_lastRect.bottom - m_lastRect.top;
    if (!HasAnimatedProcessingProvider()) {
        return false;
    }

    return m_mode == IslandDisplayMode::AgentCompact ||
        m_mode == IslandDisplayMode::AgentExpanded ||
        ShouldShowWorkingEdgeBadge(m_mode, lastHeight);
}

void AgentSessionsComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    (void)currentTimeMs;
    if (!m_res || !m_res->d2dContext || !IsActive() || contentAlpha <= 0.01f) {
        return;
    }

    m_lastRect = rect;
    if (m_mode == IslandDisplayMode::AgentCompact) {
        DrawCompact(rect, contentAlpha);
    } else {
        DrawExpanded(rect, contentAlpha);
    }
}

bool AgentSessionsComponent::OnMouseWheel(float x, float y, int delta) {
    if (m_mode != IslandDisplayMode::AgentExpanded || !Contains(m_lastRect, x, y) || delta == 0) {
        return false;
    }

    if (Contains(m_historyViewportRect, x, y) && m_historyMaxScroll > 0.0f) {
        m_historyScroll = Clamp(m_historyScroll - (delta > 0 ? kScrollStep : -kScrollStep), 0.0f, m_historyMaxScroll);
        return true;
    }

    if (Contains(m_listViewportRect, x, y) && m_sessionListMaxScroll > 0.0f) {
        m_sessionListScroll = Clamp(m_sessionListScroll - (delta > 0 ? kScrollStep : -kScrollStep), 0.0f, m_sessionListMaxScroll);
        return true;
    }

    return false;
}

bool AgentSessionsComponent::OnMouseMove(float x, float y) {
    const HitTarget hit = HitTest(x, y);
    const bool changed = hit.kind != m_hoveredKind ||
        hit.sessionKind != m_hoveredSessionKind ||
        hit.sessionId != m_hoveredSessionId;

    m_hoveredKind = hit.kind;
    m_hoveredSessionKind = hit.sessionKind;
    m_hoveredSessionId = hit.sessionId;
    return changed;
}

bool AgentSessionsComponent::OnMouseClick(float x, float y) {
    if (!Contains(m_lastRect, x, y)) {
        return false;
    }

    const HitTarget hit = HitTest(x, y);
    switch (hit.kind) {
    case HitKind::CompactPet: {
        const auto providers = AvailableCompactProviders();
        if (providers.size() <= 1) {
            if (m_onRequestOpenExpanded) {
                m_onRequestOpenExpanded();
            }
        } else if (m_onCompactChooserOpenChanged) {
            m_onCompactChooserOpenChanged(!m_compactChooserOpen);
        }
        return true;
    }
    case HitKind::CompactExpandBody:
        if (m_onRequestOpenExpanded) {
            m_onRequestOpenExpanded();
        }
        return true;
    case HitKind::CompactChooseClaude:
        if (m_onCompactProviderChanged) {
            m_onCompactProviderChanged(AgentKind::Claude);
        }
        if (m_onCompactChooserOpenChanged) {
            m_onCompactChooserOpenChanged(false);
        }
        return true;
    case HitKind::CompactChooseCodex:
        if (m_onCompactProviderChanged) {
            m_onCompactProviderChanged(AgentKind::Codex);
        }
        if (m_onCompactChooserOpenChanged) {
            m_onCompactChooserOpenChanged(false);
        }
        return true;
    case HitKind::CompactDismissChooser:
        if (m_onCompactChooserOpenChanged) {
            m_onCompactChooserOpenChanged(false);
        }
        return true;
    case HitKind::FilterClaude:
        if (m_onFilterChanged) {
            m_onFilterChanged(AgentSessionFilter::Claude);
        }
        return true;
    case HitKind::FilterCodex:
        if (m_onFilterChanged) {
            m_onFilterChanged(AgentSessionFilter::Codex);
        }
        return true;
    case HitKind::SessionRow:
        if (m_onSessionSelected) {
            m_onSessionSelected(hit.sessionKind, hit.sessionId);
        }
        return true;
    case HitKind::ApprovePermission:
        if (m_onApprovePermission) {
            m_onApprovePermission(hit.sessionId);
        }
        return true;
    case HitKind::DenyPermission:
        if (m_onDenyPermission) {
            m_onDenyPermission(hit.sessionId);
        }
        return true;
    case HitKind::None:
        if (Contains(m_surfaceRect, x, y) &&
            !Contains(m_listCardRect, x, y) &&
            !Contains(m_historyCardRect, x, y) &&
            m_onRequestCloseExpanded) {
            m_onRequestCloseExpanded();
        }
        return true;
    default:
        return true;
    }
}

void AgentSessionsComponent::DrawCompact(const D2D1_RECT_F& rect, float alpha) {
    if (m_summaries.empty()) {
        m_hits.clear();
        return;
    }

    const auto providers = AvailableCompactProviders();
    if (providers.empty()) {
        m_hits.clear();
        return;
    }

    const AgentKind provider = ResolveCompactProvider();
    const AgentSessionSummary* compactSummary = FindSummary(provider);
    if (!compactSummary) {
        m_hits.clear();
        return;
    }

    const D2D1_RECT_F compactRect = D2D1::RectF(rect.left + 4.0f, rect.top + 2.0f, rect.right - 4.0f, rect.bottom - 2.0f);
    m_hits.clear();
    const float badgeWidth = Constants::Size::AGENT_EDGE_BADGE_WIDTH;
    const D2D1_RECT_F badgeRect = D2D1::RectF(compactRect.left, compactRect.top, compactRect.left + badgeWidth, compactRect.bottom);
    const D2D1_RECT_F contentRect = D2D1::RectF(badgeRect.right + 6.0f, compactRect.top, compactRect.right, compactRect.bottom);

    if (provider == AgentKind::Claude) {
        DrawClaudeCompactVisual(compactRect, alpha, *compactSummary, GetClaudePrimaryPhase());
    } else {
        DrawCodexCompactVisual(compactRect, alpha, *compactSummary);
    }

    m_hits.push_back({ HitKind::CompactPet, provider, compactSummary->sessionId, badgeRect });
    if (m_compactChooserOpen && providers.size() > 1) {
        DrawCompactProviderChooser(contentRect, alpha, providers);
        m_hits.push_back({ HitKind::CompactDismissChooser, provider, compactSummary->sessionId, contentRect });
    } else {
        m_hits.push_back({ HitKind::CompactExpandBody, provider, compactSummary->sessionId, contentRect });
    }
}

bool AgentSessionsComponent::ShouldShowWorkingEdgeBadge(IslandDisplayMode mode, float islandHeight) const {
    if (!HasAnimatedProcessingProvider()) {
        return false;
    }
    if (islandHeight < Constants::Size::COMPACT_MIN_HEIGHT) {
        return false;
    }

    return mode == IslandDisplayMode::Idle ||
        mode == IslandDisplayMode::MusicCompact ||
        mode == IslandDisplayMode::PomodoroCompact ||
        mode == IslandDisplayMode::TodoListCompact;
}

void AgentSessionsComponent::DrawWorkingEdgeBadge(const D2D1_RECT_F& rect, float alpha) {
    if (!m_res || !m_res->d2dContext) {
        return;
    }

    const AgentKind provider = ResolveWorkingBadgeProvider();
    const D2D1_COLOR_F accent = provider == AgentKind::Codex
        ? AccentColor(AgentKind::Codex)
        : PhaseColor(ClaudeVisualPhase::Processing);
    ComPtr<ID2D1SolidColorBrush> glowBrush;
    m_res->d2dContext->CreateSolidColorBrush(accent, &glowBrush);
    if (glowBrush) {
        glowBrush->SetOpacity(0.16f * alpha);
        m_res->d2dContext->FillRoundedRectangle(
            D2D1::RoundedRect(rect, 10.0f, 10.0f),
            glowBrush.Get());
    }

    const D2D1_RECT_F badgeIconRect = D2D1::RectF(rect.left + 2.0f, rect.top + 3.0f, rect.right - 2.0f, rect.bottom - 3.0f);
    if (provider == AgentKind::Codex) {
        DrawCodexPetIcon(badgeIconRect, alpha, true);
    } else {
        DrawClaudeCrabIcon(badgeIconRect, alpha, true);
    }
}

void AgentSessionsComponent::DrawClaudeCompactVisual(const D2D1_RECT_F& rect, float alpha, const AgentSessionSummary& summary, ClaudeVisualPhase phase) {
    auto* ctx = m_res->d2dContext;
    const float height = rect.bottom - rect.top;
    const float badgeWidth = Constants::Size::AGENT_EDGE_BADGE_WIDTH;
    const D2D1_RECT_F badgeRect = D2D1::RectF(rect.left, rect.top, rect.left + badgeWidth, rect.bottom);
    const D2D1_RECT_F hudRect = D2D1::RectF(badgeRect.right + 18.0f, rect.top + 1.0f, rect.right - 4.0f, rect.bottom - 1.0f);
    const float crabWidth = (std::min)(badgeWidth - 4.0f, 26.0f);
    const float crabHeight = (std::min)(height - 4.0f, 26.0f);
    const float centerX = badgeRect.left + badgeWidth * 0.5f;
    const D2D1_RECT_F crabRect = D2D1::RectF(
        centerX - crabWidth * 0.5f,
        badgeRect.top + (height - crabHeight) * 0.5f,
        centerX + crabWidth * 0.5f,
        badgeRect.top + (height + crabHeight) * 0.5f);

    ComPtr<ID2D1SolidColorBrush> haloBrush;
    ctx->CreateSolidColorBrush(PhaseColor(phase), &haloBrush);
    if (haloBrush) {
        haloBrush->SetOpacity((phase == ClaudeVisualPhase::Processing ? 0.18f : 0.08f) * alpha);
        ctx->FillEllipse(
            D2D1::Ellipse(
                D2D1::Point2F((crabRect.left + crabRect.right) * 0.5f, (crabRect.top + crabRect.bottom) * 0.5f),
                crabWidth * 0.54f,
                crabHeight * 0.58f),
            haloBrush.Get());
        haloBrush->SetOpacity((phase == ClaudeVisualPhase::Processing ? 0.24f : 0.12f) * alpha);
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(crabRect.left + 5.0f, crabRect.bottom - 4.5f, crabRect.right - 5.0f, crabRect.bottom - 1.5f),
                2.0f, 2.0f),
            haloBrush.Get());
    }

    DrawClaudeCrabIcon(crabRect, alpha, phase == ClaudeVisualPhase::Processing);

    if (phase != ClaudeVisualPhase::Idle) {
        const D2D1_RECT_F statusRect = D2D1::RectF(
            crabRect.right - 1.0f,
            crabRect.top + 0.0f,
            crabRect.right + 14.0f,
            crabRect.top + 16.0f);
        DrawClaudeCompactStatus(phase, statusRect, alpha);
    }

    DrawCompactHud(summary, hudRect, alpha);
}

void AgentSessionsComponent::DrawCodexCompactVisual(const D2D1_RECT_F& rect, float alpha, const AgentSessionSummary& summary) {
    auto* ctx = m_res->d2dContext;
    const float badgeWidth = Constants::Size::AGENT_EDGE_BADGE_WIDTH;
    const D2D1_RECT_F badgeRect = D2D1::RectF(rect.left, rect.top, rect.left + badgeWidth, rect.bottom);
    const D2D1_RECT_F bodyRect = D2D1::RectF(badgeRect.right + 10.0f, rect.top + 2.0f, rect.right - 6.0f, rect.bottom - 2.0f);
    const bool active = summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting;
    const float pulse = PulseWave(m_animationTime, active ? 3.6f : 1.5f, 0.10f, active ? 0.24f : 0.16f);
    const float scanProgress = std::fmod(m_animationTime * 0.24f, 1.0f);

    ComPtr<ID2D1SolidColorBrush> panelBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.17f, 0.24f, 1.0f), &panelBrush);
    if (panelBrush) {
        panelBrush->SetOpacity(0.78f * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(bodyRect, 10.0f, 10.0f), panelBrush.Get());
        panelBrush->SetColor(D2D1::ColorF(0.26f, 0.50f, 0.84f, 1.0f));
        panelBrush->SetOpacity((active ? 0.36f : 0.18f) * alpha);
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(bodyRect, 10.0f, 10.0f), panelBrush.Get(), active ? 1.3f : 1.0f);
    }

    ComPtr<ID2D1SolidColorBrush> glowBrush;
    ctx->CreateSolidColorBrush(AccentColor(AgentKind::Codex), &glowBrush);
    if (glowBrush) {
        glowBrush->SetOpacity((active ? 0.18f : 0.08f) * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(badgeRect.left + 2.0f, badgeRect.top + 3.0f, badgeRect.right - 2.0f, badgeRect.bottom - 3.0f),
            9.0f, 9.0f), glowBrush.Get());
        glowBrush->SetOpacity(pulse * alpha);
        ctx->DrawEllipse(
            D2D1::Ellipse(
                D2D1::Point2F((badgeRect.left + badgeRect.right) * 0.5f, (badgeRect.top + badgeRect.bottom) * 0.5f),
                12.0f,
                12.0f),
            glowBrush.Get(),
            1.0f);

        if (active) {
            const float scanWidth = 18.0f;
            const float scanCenter = bodyRect.left + scanProgress * (bodyRect.right - bodyRect.left);
            glowBrush->SetOpacity(0.11f * alpha);
            ctx->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(
                        (std::max)(bodyRect.left + 4.0f, scanCenter - scanWidth * 0.5f),
                        bodyRect.top + 4.0f,
                        (std::min)(bodyRect.right - 4.0f, scanCenter + scanWidth * 0.5f),
                        bodyRect.bottom - 4.0f),
                    8.0f, 8.0f),
                glowBrush.Get());
        }
    }

    DrawCodexPetIcon(D2D1::RectF(badgeRect.left + 1.0f, badgeRect.top + 2.0f, badgeRect.right - 1.0f, badgeRect.bottom - 2.0f), alpha, active);

    if (summary.hasHudData) {
        DrawCompactHud(summary, bodyRect, alpha);
    } else {
        DrawTextLine(L"Codex", m_tinyFormat.Get(),
            D2D1::RectF(bodyRect.left, bodyRect.top, bodyRect.right, bodyRect.top + 13.0f),
            D2D1::ColorF(0.90f, 0.90f, 0.95f, 1.0f), alpha, false);

        const std::wstring detail = !summary.recentActivityText.empty()
            ? summary.recentActivityText
            : (summary.statusText.empty() ? L"Idle" : summary.statusText);
        DrawTextLine(detail, m_tinyFormat.Get(),
            D2D1::RectF(bodyRect.left, bodyRect.top + 13.0f, bodyRect.right, bodyRect.bottom),
            D2D1::ColorF(0.68f, 0.76f, 0.92f, 1.0f), alpha, false);
    }
}

void AgentSessionsComponent::DrawClaudeCrabIcon(const D2D1_RECT_F& rect, float alpha, bool animateLegs) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(kClaudePromptR, kClaudePromptG, kClaudePromptB, 1.0f), &brush);
    if (!brush) {
        return;
    }

    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float scale = (std::min)(width / 66.0f, height / 52.0f);
    const float crabWidth = 66.0f * scale;
    const float crabHeight = 52.0f * scale;
    const float originX = rect.left + (width - crabWidth) * 0.5f;
    const float bodyBob = animateLegs ? std::sin(m_animationTime * 2.4f) * 0.9f * scale : 0.0f;
    const float originY = rect.top + (height - crabHeight) * 0.5f + bodyBob;
    const float legOffsets[4] = {
        animateLegs ? std::sin(m_animationTime * 3.8f + 0.0f) * 2.2f : 0.0f,
        animateLegs ? std::sin(m_animationTime * 3.8f + 1.8f) * 1.8f : 0.0f,
        animateLegs ? std::sin(m_animationTime * 3.8f + 0.9f) * 2.0f : 0.0f,
        animateLegs ? std::sin(m_animationTime * 3.8f + 2.6f) * 1.7f : 0.0f
    };
    const float legX[] = { 6.0f, 18.0f, 42.0f, 54.0f };
    const bool blink = animateLegs && std::fmod(m_animationTime, 3.6f) > 3.18f;
    const float eyeBottom = blink ? 16.0f * scale : 19.5f * scale;

    brush->SetOpacity(alpha);
    ctx->FillRectangle(D2D1::RectF(originX, originY + 13.0f * scale, originX + 6.0f * scale, originY + 26.0f * scale), brush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 60.0f * scale, originY + 13.0f * scale, originX + 66.0f * scale, originY + 26.0f * scale), brush.Get());

    for (int index = 0; index < 4; ++index) {
        const float legHeight = (13.0f + legOffsets[index]) * scale;
        ctx->FillRectangle(D2D1::RectF(
            originX + legX[index] * scale,
            originY + 39.0f * scale,
            originX + (legX[index] + 6.0f) * scale,
            originY + 39.0f * scale + legHeight), brush.Get());
    }

    ctx->FillRectangle(D2D1::RectF(originX + 6.0f * scale, originY, originX + 60.0f * scale, originY + 39.0f * scale), brush.Get());

    brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    ctx->FillRectangle(D2D1::RectF(originX + 12.0f * scale, originY + 13.0f * scale, originX + 18.0f * scale, originY + eyeBottom), brush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 48.0f * scale, originY + 13.0f * scale, originX + 54.0f * scale, originY + eyeBottom), brush.Get());
}

void AgentSessionsComponent::DrawCodexPetIcon(const D2D1_RECT_F& rect, float alpha, bool animate) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> bodyBrush;
    ctx->CreateSolidColorBrush(AccentColor(AgentKind::Codex), &bodyBrush);
    if (!bodyBrush) {
        return;
    }

    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float scale = (std::min)(width / 32.0f, height / 28.0f);
    const float bodyWidth = 32.0f * scale;
    const float bodyHeight = 28.0f * scale;
    const float originX = rect.left + (width - bodyWidth) * 0.5f;
    const float bodyBob = animate ? std::sin(m_animationTime * 2.2f) * 0.7f * scale : 0.0f;
    const float originY = rect.top + (height - bodyHeight) * 0.5f + bodyBob;
    const float footOffset = animate ? std::sin(m_animationTime * 3.4f) * 1.0f * scale : 0.0f;
    const float antennaOffset = animate ? std::sin(m_animationTime * 2.3f + 0.8f) * 0.7f * scale : 0.0f;
    const float eyeShift = animate ? std::sin(m_animationTime * 1.8f + 0.6f) * 0.3f * scale : 0.0f;

    bodyBrush->SetOpacity(alpha);
    ctx->FillRectangle(D2D1::RectF(originX + 6.0f * scale, originY + 6.0f * scale, originX + 26.0f * scale, originY + 20.0f * scale), bodyBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 10.0f * scale, originY + 2.0f * scale - antennaOffset, originX + 12.0f * scale, originY + 6.0f * scale), bodyBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 20.0f * scale, originY + 2.0f * scale + antennaOffset, originX + 22.0f * scale, originY + 6.0f * scale), bodyBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 2.0f * scale, originY + 10.0f * scale, originX + 6.0f * scale, originY + 14.0f * scale), bodyBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 26.0f * scale, originY + 10.0f * scale, originX + 30.0f * scale, originY + 14.0f * scale), bodyBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 9.0f * scale, originY + 20.0f * scale + footOffset, originX + 13.0f * scale, originY + 26.0f * scale + footOffset), bodyBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 19.0f * scale, originY + 20.0f * scale - footOffset, originX + 23.0f * scale, originY + 26.0f * scale - footOffset), bodyBrush.Get());

    ComPtr<ID2D1SolidColorBrush> eyeBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &eyeBrush);
    if (!eyeBrush) {
        return;
    }

    eyeBrush->SetOpacity(alpha);
    const float blinkHeight = animate && std::fmod(m_animationTime, 3.8f) > 3.35f ? 1.5f * scale : 3.0f * scale;
    ctx->FillRectangle(D2D1::RectF(originX + 11.0f * scale + eyeShift, originY + 10.0f * scale, originX + 13.0f * scale + eyeShift, originY + 10.0f * scale + blinkHeight), eyeBrush.Get());
    ctx->FillRectangle(D2D1::RectF(originX + 19.0f * scale - eyeShift, originY + 10.0f * scale, originX + 21.0f * scale - eyeShift, originY + 10.0f * scale + blinkHeight), eyeBrush.Get());
}

void AgentSessionsComponent::DrawCompactProviderChooser(const D2D1_RECT_F& rect, float alpha, const std::vector<AgentKind>& providers) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> overlayBrush;
    ctx->CreateSolidColorBrush(CardFill(), &overlayBrush);
    if (!overlayBrush) {
        return;
    }

    overlayBrush->SetOpacity(0.96f * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), overlayBrush.Get());
    overlayBrush->SetColor(StrokeColor());
    overlayBrush->SetOpacity(0.72f * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), overlayBrush.Get(), 1.0f);

    const float chipGap = providers.size() > 1 ? 8.0f : 0.0f;
    const float chipWidth = ((rect.right - rect.left) - chipGap) / static_cast<float>(providers.size());
    float chipLeft = rect.left;
    for (size_t index = 0; index < providers.size(); ++index) {
        const AgentKind kind = providers[index];
        const D2D1_RECT_F chipRect = D2D1::RectF(chipLeft, rect.top + 2.0f, chipLeft + chipWidth, rect.bottom - 2.0f);
        const bool selected = kind == ResolveCompactProvider();
        DrawPill(
            chipRect,
            L"",
            selected,
            alpha,
            (kind == AgentKind::Claude && m_hoveredKind == HitKind::CompactChooseClaude) ||
            (kind == AgentKind::Codex && m_hoveredKind == HitKind::CompactChooseCodex));
        DrawSourceGlyph(kind, D2D1::RectF(chipRect.left + 4.0f, chipRect.top + 4.0f, chipRect.left + 28.0f, chipRect.bottom - 4.0f), alpha);
        DrawTextLine(
            kind == AgentKind::Claude ? L"Claude" : L"Codex",
            m_tinyFormat.Get(),
            D2D1::RectF(chipRect.left + 28.0f, chipRect.top, chipRect.right - 4.0f, chipRect.bottom),
            selected ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0.92f, 0.92f, 0.96f, 1.0f),
            alpha,
            false);
        m_hits.push_back({ kind == AgentKind::Claude ? HitKind::CompactChooseClaude : HitKind::CompactChooseCodex, kind, L"", chipRect });
        chipLeft += chipWidth + chipGap;
    }
}

void AgentSessionsComponent::DrawClaudeCompactStatus(ClaudeVisualPhase phase, const D2D1_RECT_F& rect, float alpha) {
    switch (phase) {
    case ClaudeVisualPhase::Processing:
        DrawProcessingSpinner(rect, alpha);
        break;
    case ClaudeVisualPhase::WaitingForApproval:
        DrawPixelIcon(rect, D2D1::ColorF(1.0f, 0.7f, 0.0f, 1.0f), kApprovalPixels, _countof(kApprovalPixels), nullptr, 0, alpha);
        break;
    case ClaudeVisualPhase::WaitingForInput:
        DrawPixelIcon(rect, D2D1::ColorF(0.4f, 0.75f, 0.45f, 1.0f), kReadyPixels, _countof(kReadyPixels), nullptr, 0, alpha);
        break;
    case ClaudeVisualPhase::Idle:
    default:
        break;
    }
}

void AgentSessionsComponent::DrawProcessingSpinner(const D2D1_RECT_F& rect, float alpha) {
    const size_t index = static_cast<size_t>(std::fmod(std::floor(m_animationTime / 0.22f), 6.0f));
    DrawTextLine(kSpinnerSymbols[index], m_symbolFormat.Get() ? m_symbolFormat.Get() : m_bodyFormat.Get(),
        rect, D2D1::ColorF(kClaudePromptR, kClaudePromptG, kClaudePromptB, 1.0f), alpha, false, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void AgentSessionsComponent::DrawCompactHud(const AgentSessionSummary& summary, const D2D1_RECT_F& rect, float alpha) {
    if (!summary.hasHudData) {
        DrawTextLine(summary.statusText.empty() ? L"Hud unavailable" : summary.statusText,
            m_tinyFormat.Get(), rect, D2D1::ColorF(0.82f, 0.82f, 0.86f, 1.0f), alpha, false);
        return;
    }

    auto* ctx = m_res->d2dContext;
    const float width = rect.right - rect.left;
    const D2D1_RECT_F hudCard = D2D1::RectF(rect.left, rect.top + 1.0f, rect.right, rect.bottom);
    const D2D1_RECT_F line1 = D2D1::RectF(rect.left + 8.0f, rect.top + 2.0f, rect.right - 8.0f, rect.top + 14.0f);
    const D2D1_RECT_F line2 = D2D1::RectF(rect.left + 8.0f, rect.top + 13.0f, rect.right - 8.0f, rect.top + 25.0f);
    const D2D1_RECT_F line3 = D2D1::RectF(rect.left + 8.0f, rect.top + 24.0f, rect.right - 8.0f, rect.bottom - 2.0f);

    const D2D1_COLOR_F providerAccent = summary.kind == AgentKind::Codex
        ? D2D1::ColorF(0.44f, 0.74f, 1.0f, 1.0f)
        : D2D1::ColorF(0.40f, 0.78f, 0.82f, 1.0f);
    const bool active = summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting;
    const float shimmer = PulseWave(m_animationTime, active ? 3.0f : 1.2f, 0.08f, active ? 0.18f : 0.12f);

    ComPtr<ID2D1SolidColorBrush> hudBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.12f, 0.16f, 1.0f), &hudBrush);
    if (hudBrush) {
        hudBrush->SetOpacity((summary.kind == AgentKind::Codex ? 0.92f : 0.72f) * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(hudCard, 9.0f, 9.0f), hudBrush.Get());
    }

    std::wstring model = summary.modelName.empty() ? (summary.kind == AgentKind::Codex ? L"Codex" : L"Claude") : summary.modelName;
    DrawTextLine(model, m_tinyFormat.Get(), line1, D2D1::ColorF(0.90f, 0.90f, 0.94f, 1.0f), alpha, false);

    const std::wstring contextLine = L"CTX " + RemainingPercentText(summary.contextUsedPercent);
    DrawTextLine(contextLine, m_tinyFormat.Get(), D2D1::RectF(line2.left, line2.top, line2.left + 56.0f, line2.bottom), providerAccent, alpha, false);

    const D2D1_RECT_F ctxTrackRect = D2D1::RectF(line2.left + 58.0f, line2.top + 4.0f, line2.right, line2.bottom - 2.0f);
    ComPtr<ID2D1SolidColorBrush> meterBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &meterBrush);
    if (meterBrush) {
        meterBrush->SetOpacity(0.10f * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(ctxTrackRect, 4.0f, 4.0f), meterBrush.Get());
        meterBrush->SetColor(providerAccent);
        meterBrush->SetOpacity((active ? 0.88f : 0.72f) * alpha);
        const float usedRatio = summary.contextUsedPercent >= 0 ? Clamp(static_cast<float>(summary.contextUsedPercent) / 100.0f, 0.0f, 1.0f) : 0.0f;
        const D2D1_RECT_F ctxFillRect = D2D1::RectF(
            ctxTrackRect.left,
            ctxTrackRect.top,
            ctxTrackRect.left + (ctxTrackRect.right - ctxTrackRect.left) * usedRatio,
            ctxTrackRect.bottom);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(ctxFillRect, 4.0f, 4.0f), meterBrush.Get());
        if (active && usedRatio > 0.08f) {
            const float scanWidth = 10.0f;
            const float scanCenter = ctxFillRect.left + std::fmod(m_animationTime * 11.0f, (std::max)(scanWidth, ctxFillRect.right - ctxFillRect.left));
            meterBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
            meterBrush->SetOpacity(0.14f * alpha);
            ctx->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(
                        (std::max)(ctxFillRect.left, scanCenter - scanWidth * 0.5f),
                        ctxFillRect.top,
                        (std::min)(ctxFillRect.right, scanCenter + scanWidth * 0.5f),
                        ctxFillRect.bottom),
                    4.0f, 4.0f),
                meterBrush.Get());
        }
    }

    std::wstring usageLine = L"5H " + RemainingPercentText(summary.fiveHourUsedPercent);
    if (width > 92.0f) {
        usageLine += L"   7D " + RemainingPercentText(summary.sevenDayUsedPercent);
    }
    const D2D1_COLOR_F fiveHourColor = QuotaThresholdColor(summary.fiveHourUsedPercent);
    const D2D1_COLOR_F sevenDayColor = QuotaThresholdColor(summary.sevenDayUsedPercent);
    if (width > 92.0f) {
        const D2D1_RECT_F fiveHourRect = D2D1::RectF(line3.left, line3.top, line3.left + width * 0.48f, line3.bottom);
        const D2D1_RECT_F sevenDayRect = D2D1::RectF(line3.left + width * 0.52f, line3.top, line3.right, line3.bottom);
        DrawTextLine(L"5H " + RemainingPercentText(summary.fiveHourUsedPercent), m_tinyFormat.Get(),
            fiveHourRect,
            fiveHourColor, alpha, false);
        DrawTextLine(L"7D " + RemainingPercentText(summary.sevenDayUsedPercent), m_tinyFormat.Get(),
            sevenDayRect,
            sevenDayColor, alpha, false);
    } else {
        DrawTextLine(usageLine, m_tinyFormat.Get(), line3, fiveHourColor, alpha, false);
    }
}

void AgentSessionsComponent::DrawClaudeStatusIcon(ClaudeVisualPhase phase, const D2D1_RECT_F& rect, float alpha) {
    switch (phase) {
    case ClaudeVisualPhase::Processing:
        DrawPixelIcon(rect, D2D1::ColorF(0.0f, 0.8f, 0.8f, 1.0f),
            kRunningSolidPixels, _countof(kRunningSolidPixels),
            kRunningFadedPixels, _countof(kRunningFadedPixels), alpha);
        break;
    case ClaudeVisualPhase::WaitingForApproval:
        DrawPixelIcon(rect, D2D1::ColorF(1.0f, 0.7f, 0.0f, 1.0f),
            kApprovalPixels, _countof(kApprovalPixels), nullptr, 0, alpha);
        break;
    case ClaudeVisualPhase::WaitingForInput:
        DrawPixelIcon(rect, D2D1::ColorF(0.4f, 0.75f, 0.45f, 1.0f),
            kWaitingInputSolidPixels, _countof(kWaitingInputSolidPixels),
            kWaitingInputFadedPixels, _countof(kWaitingInputFadedPixels), alpha);
        break;
    case ClaudeVisualPhase::Idle:
    default:
        DrawPixelIcon(rect, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f),
            kIdlePixels, _countof(kIdlePixels), nullptr, 0, alpha);
        break;
    }
}

void AgentSessionsComponent::DrawExpanded(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    m_hits.clear();

    const D2D1_RECT_F surface = D2D1::RectF(rect.left + 10.0f, rect.top + 8.0f, rect.right - 10.0f, rect.bottom - 10.0f);
    const D2D1_RECT_F header = D2D1::RectF(surface.left + 10.0f, surface.top + 10.0f, surface.right - 10.0f, surface.top + 48.0f);
    const D2D1_RECT_F listCard = D2D1::RectF(surface.left + 10.0f, header.bottom + 8.0f, surface.left + 176.0f, surface.bottom - 10.0f);
    const D2D1_RECT_F historyCard = D2D1::RectF(listCard.right + 10.0f, header.bottom + 8.0f, surface.right - 10.0f, surface.bottom - 10.0f);
    m_surfaceRect = surface;
    m_listCardRect = listCard;
    m_historyCardRect = historyCard;

    m_listViewportRect = D2D1::RectF(listCard.left + 8.0f, listCard.top + 42.0f, listCard.right - 8.0f, listCard.bottom - 8.0f);
    m_historyViewportRect = D2D1::RectF(historyCard.left + 10.0f, historyCard.top + 38.0f, historyCard.right - 10.0f, historyCard.bottom - 10.0f);

    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SurfaceFill(), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity(0.95f * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(surface, kExpandedCorner, kExpandedCorner), brush.Get());
    brush->SetColor(StrokeColor());
    brush->SetOpacity(0.85f * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(surface, kExpandedCorner, kExpandedCorner), brush.Get(), 1.0f);

    brush->SetColor(CardFill());
    brush->SetOpacity(0.98f * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(listCard, 14.0f, 14.0f), brush.Get());
    ctx->FillRoundedRectangle(D2D1::RoundedRect(historyCard, 14.0f, 14.0f), brush.Get());
    brush->SetColor(StrokeColor());
    brush->SetOpacity(0.60f * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(listCard, 14.0f, 14.0f), brush.Get(), 1.0f);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(historyCard, 14.0f, 14.0f), brush.Get(), 1.0f);

    const D2D1_RECT_F titleRect = D2D1::RectF(header.left, header.top, header.left + 170.0f, header.bottom);
    DrawTextLine(L"Session Center", m_headingFormat.Get(), titleRect, D2D1::ColorF(1, 1, 1, 1), alpha, false);

    ComPtr<ID2D1SolidColorBrush> dividerBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(kClaudePromptR, kClaudePromptG, kClaudePromptB, 1.0f), &dividerBrush);
    if (dividerBrush) {
        dividerBrush->SetOpacity(0.16f * alpha);
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(surface.left + 14.0f, header.bottom + 1.0f, surface.right - 14.0f, header.bottom + 4.0f),
                1.5f, 1.5f),
            dividerBrush.Get());
    }

    const D2D1_RECT_F filterClaude = D2D1::RectF(header.right - 136.0f, header.top + 8.0f, header.right - 66.0f, header.top + 32.0f);
    const D2D1_RECT_F filterCodex = D2D1::RectF(filterClaude.right + 8.0f, header.top + 8.0f, filterClaude.right + 74.0f, header.top + 32.0f);

    DrawPill(filterClaude, L"Claude", m_filter == AgentSessionFilter::Claude, alpha, m_hoveredKind == HitKind::FilterClaude);
    DrawPill(filterCodex, L"Codex", m_filter == AgentSessionFilter::Codex, alpha, m_hoveredKind == HitKind::FilterCodex);

    m_hits.push_back({ HitKind::FilterClaude, AgentKind::Claude, L"", filterClaude });
    m_hits.push_back({ HitKind::FilterCodex, AgentKind::Codex, L"", filterCodex });

    DrawTextLine(L"Recent Sessions", m_bodyFormat.Get(),
        D2D1::RectF(listCard.left + 10.0f, listCard.top + 8.0f, listCard.right - 10.0f, listCard.top + 28.0f),
        D2D1::ColorF(0.84f, 0.84f, 0.88f, 1.0f), alpha, false);

    std::vector<const AgentSessionSummary*> filtered = FilteredSessions();
    const AgentSessionSummary* selected = FindSelectedSummary();
    if (!selected && !filtered.empty()) {
        selected = filtered.front();
    }

    float rowTop = m_listViewportRect.top - m_sessionListScroll;
    constexpr float rowHeight = 58.0f;
    ctx->PushAxisAlignedClip(m_listViewportRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (const AgentSessionSummary* summary : filtered) {
        if (!summary) {
            continue;
        }

        const D2D1_RECT_F rowRect = D2D1::RectF(m_listViewportRect.left, rowTop, m_listViewportRect.right, rowTop + rowHeight);
        if (rowRect.bottom >= m_listViewportRect.top - rowHeight && rowRect.top <= m_listViewportRect.bottom + rowHeight) {
            const bool isSelected = selected &&
                selected->kind == summary->kind &&
                selected->sessionId == summary->sessionId;
            const bool hovered = m_hoveredKind == HitKind::SessionRow &&
                m_hoveredSessionKind == summary->kind &&
                m_hoveredSessionId == summary->sessionId;
            DrawSessionRow(*summary, rowRect, alpha, isSelected, hovered);
        }

        if (rowRect.bottom >= m_listViewportRect.top && rowRect.top <= m_listViewportRect.bottom) {
            const D2D1_RECT_F hitRect = D2D1::RectF(
                rowRect.left,
                (std::max)(rowRect.top, m_listViewportRect.top),
                rowRect.right,
                (std::min)(rowRect.bottom, m_listViewportRect.bottom));
            m_hits.push_back({ HitKind::SessionRow, summary->kind, summary->sessionId, hitRect });
        }
        rowTop += rowHeight + 8.0f;
    }
    ctx->PopAxisAlignedClip();

    const float listContentHeight = filtered.empty() ? 0.0f : (rowTop - (m_listViewportRect.top - m_sessionListScroll));
    m_sessionListMaxScroll = (std::max)(0.0f, listContentHeight - (m_listViewportRect.bottom - m_listViewportRect.top));
    m_sessionListScroll = Clamp(m_sessionListScroll, 0.0f, m_sessionListMaxScroll);

    if (filtered.empty()) {
        DrawTextLine(L"No sessions available", m_bodyFormat.Get(), m_listViewportRect,
            D2D1::ColorF(0.78f, 0.78f, 0.82f, 1.0f), alpha, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    DrawTextLine(L"Overview", m_bodyFormat.Get(),
        D2D1::RectF(historyCard.left + 10.0f, historyCard.top + 8.0f, historyCard.right - 10.0f, historyCard.top + 28.0f),
        D2D1::ColorF(0.84f, 0.84f, 0.88f, 1.0f), alpha, false);

    if (!selected) {
        m_historyMaxScroll = 0.0f;
        m_historyScroll = 0.0f;
        DrawTextLine(L"Choose a session to inspect recent activity.", m_bodyFormat.Get(), m_historyViewportRect,
            D2D1::ColorF(0.78f, 0.78f, 0.82f, 1.0f), alpha, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        float overviewTop = m_historyViewportRect.top;
        if (selected->kind == AgentKind::Claude && selected->phase == AgentSessionPhase::WaitingForApproval && !selected->pendingToolUseId.empty()) {
            const D2D1_RECT_F allowRect = D2D1::RectF(historyCard.left + 10.0f, historyCard.top + 34.0f, historyCard.left + 100.0f, historyCard.top + 60.0f);
            const D2D1_RECT_F denyRect = D2D1::RectF(allowRect.right + 8.0f, historyCard.top + 34.0f, allowRect.right + 86.0f, historyCard.top + 60.0f);
            DrawPill(allowRect, L"Allow", true, alpha, m_hoveredKind == HitKind::ApprovePermission);
            DrawPill(denyRect, L"Deny", false, alpha, m_hoveredKind == HitKind::DenyPermission);
            m_hits.push_back({ HitKind::ApprovePermission, AgentKind::Claude, selected->pendingToolUseId, allowRect });
            m_hits.push_back({ HitKind::DenyPermission, AgentKind::Claude, selected->pendingToolUseId, denyRect });
            overviewTop = historyCard.top + 70.0f;
            m_historyViewportRect = D2D1::RectF(historyCard.left + 10.0f, overviewTop, historyCard.right - 10.0f, historyCard.bottom - 10.0f);
        }

        const auto overviewSections = BuildOverviewForSelectedSession();
        ctx->PushAxisAlignedClip(m_historyViewportRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float sectionTop = m_historyViewportRect.top - m_historyScroll;
        for (const auto& section : overviewSections) {
            const D2D1_RECT_F sectionRect = D2D1::RectF(m_historyViewportRect.left, sectionTop, m_historyViewportRect.right, m_historyViewportRect.bottom);
            const float usedHeight = DrawOverviewSection(section, sectionRect, alpha);
            sectionTop += usedHeight + 10.0f;
        }
        ctx->PopAxisAlignedClip();

        const float overviewHeight = (std::max)(0.0f, sectionTop - (m_historyViewportRect.top - m_historyScroll));
        m_historyMaxScroll = (std::max)(0.0f, overviewHeight - (m_historyViewportRect.bottom - m_historyViewportRect.top));
        m_historyScroll = Clamp(m_historyScroll, 0.0f, m_historyMaxScroll);
    }
}

void AgentSessionsComponent::DrawSessionRow(const AgentSessionSummary& summary, const D2D1_RECT_F& rect, float alpha, bool selected, bool hovered) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(selected ? AccentColor(summary.kind) : CardFill(), &brush);
    if (!brush) {
        return;
    }

    const float fillOpacity = selected ? 0.24f : (hovered ? 0.92f : 0.74f);
    brush->SetOpacity(fillOpacity * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), brush.Get());

    brush->SetColor(selected ? AccentColor(summary.kind) : StrokeColor());
    brush->SetOpacity(alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), brush.Get(), selected ? 1.4f : 1.0f);

    ComPtr<ID2D1SolidColorBrush> accentBrush;
    ctx->CreateSolidColorBrush(AccentColor(summary.kind), &accentBrush);
    if (accentBrush) {
        accentBrush->SetOpacity((selected ? 0.95f : (hovered ? 0.42f : 0.22f)) * alpha);
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(rect.left + 4.0f, rect.top + 8.0f, rect.left + 7.0f, rect.bottom - 8.0f),
                1.5f, 1.5f),
            accentBrush.Get());
    }

    const D2D1_RECT_F stateRect = D2D1::RectF(rect.left + 8.0f, rect.top + 12.0f, rect.left + 24.0f, rect.top + 28.0f);
    if (summary.kind == AgentKind::Claude) {
        DrawClaudeStatusIcon(PhaseForSummary(summary), stateRect, alpha);
    } else {
        const bool active = summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting;
        DrawPixelIcon(
            stateRect,
            active ? AccentColor(AgentKind::Codex) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.28f),
            active ? kRunningSolidPixels : kIdlePixels,
            active ? _countof(kRunningSolidPixels) : _countof(kIdlePixels),
            active ? kRunningFadedPixels : nullptr,
            active ? _countof(kRunningFadedPixels) : 0,
            alpha);
    }

    const D2D1_RECT_F iconPlateRect = D2D1::RectF(rect.left + 24.0f, rect.top + 8.0f, rect.left + 60.0f, rect.top + 36.0f);
    brush->SetColor(CardFill());
    brush->SetOpacity((selected ? 0.42f : 0.26f) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(iconPlateRect, 8.0f, 8.0f), brush.Get());

    const D2D1_RECT_F iconRect = D2D1::RectF(rect.left + 28.0f, rect.top + 10.0f, rect.left + 58.0f, rect.top + 34.0f);
    if (summary.kind == AgentKind::Claude) {
        DrawClaudeCrabIcon(iconRect, alpha, PhaseForSummary(summary) == ClaudeVisualPhase::Processing);
    } else {
        DrawCodexPetIcon(iconRect, alpha, summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting);
    }

    const D2D1_RECT_F sessionIdRect = D2D1::RectF(rect.right - 74.0f, rect.top + 8.0f, rect.right - 10.0f, rect.top + 24.0f);
    brush->SetColor(CardFill());
    brush->SetOpacity((selected ? 0.38f : 0.22f) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(sessionIdRect, 8.0f, 8.0f), brush.Get());
    brush->SetColor(selected ? AccentColor(summary.kind) : StrokeColor());
    brush->SetOpacity(0.85f * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(sessionIdRect, 8.0f, 8.0f), brush.Get(), 1.0f);

    const D2D1_RECT_F titleRect = D2D1::RectF(rect.left + 64.0f, rect.top + 8.0f, sessionIdRect.left - 6.0f, rect.top + 24.0f);
    const D2D1_RECT_F projectRect = D2D1::RectF(rect.left + 64.0f, rect.top + 23.0f, rect.right - 12.0f, rect.top + 39.0f);
    const D2D1_RECT_F timeRect = D2D1::RectF(rect.left + 64.0f, rect.top + 38.0f, rect.right - 12.0f, rect.bottom - 6.0f);

    DrawTextLine(summary.title.empty() ? L"Untitled session" : summary.title, m_smallFormat.Get(), titleRect,
        D2D1::ColorF(1, 1, 1, 1), alpha, false);
    DrawTextLine(ShortSessionId(summary.sessionId), m_tinyFormat.Get(), sessionIdRect,
        D2D1::ColorF(0.88f, 0.88f, 0.92f, 1.0f), alpha, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextLine(ProjectLabel(summary.projectPath), m_tinyFormat.Get(), projectRect,
        D2D1::ColorF(0.74f, 0.74f, 0.78f, 1.0f), alpha, false);
    DrawTextLine(RelativeTimeLabel(summary.lastActivityTs), m_tinyFormat.Get(), timeRect,
        D2D1::ColorF(0.60f, 0.64f, 0.70f, 1.0f), alpha, false);

}

void AgentSessionsComponent::DrawPixelIcon(const D2D1_RECT_F& rect,
    const D2D1_COLOR_F& color,
    const D2D1_POINT_2F* solidPixels,
    size_t solidCount,
    const D2D1_POINT_2F* fadedPixels,
    size_t fadedCount,
    float alpha) const {
    if (!m_res || !m_res->d2dContext || !solidPixels || solidCount == 0) {
        return;
    }

    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(color, &brush);
    if (!brush) {
        return;
    }

    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float scale = (std::min)(width, height) / 30.0f;
    const float dotSize = 4.0f * scale;
    const float offsetX = rect.left + (width - 30.0f * scale) * 0.5f;
    const float offsetY = rect.top + (height - 30.0f * scale) * 0.5f;

    brush->SetOpacity(alpha);
    for (size_t index = 0; index < solidCount; ++index) {
        const D2D1_POINT_2F point = solidPixels[index];
        ctx->FillRectangle(D2D1::RectF(
            offsetX + point.x * scale - dotSize * 0.5f,
            offsetY + point.y * scale - dotSize * 0.5f,
            offsetX + point.x * scale + dotSize * 0.5f,
            offsetY + point.y * scale + dotSize * 0.5f), brush.Get());
    }

    if (fadedPixels && fadedCount > 0) {
        brush->SetOpacity(alpha * 0.40f);
        for (size_t index = 0; index < fadedCount; ++index) {
            const D2D1_POINT_2F point = fadedPixels[index];
            ctx->FillRectangle(D2D1::RectF(
                offsetX + point.x * scale - dotSize * 0.5f,
                offsetY + point.y * scale - dotSize * 0.5f,
                offsetX + point.x * scale + dotSize * 0.5f,
                offsetY + point.y * scale + dotSize * 0.5f), brush.Get());
        }
    }
}

float AgentSessionsComponent::DrawOverviewSection(const OverviewSection& section, const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    const float innerPad = 12.0f;
    const float textWidth = (rect.right - rect.left) - innerPad * 2.0f;
    const float titleHeight = 16.0f;
    const float bodyHeight = MeasureWrappedHeight(section.body, m_smallFormat.Get(), textWidth);
    const float height = 18.0f + titleHeight + bodyHeight + 18.0f;
    const D2D1_RECT_F cardRect = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + height);
    const D2D1_RECT_F titleRect = D2D1::RectF(cardRect.left + innerPad, cardRect.top + 10.0f, cardRect.right - innerPad, cardRect.top + 28.0f);
    const D2D1_RECT_F bodyRect = D2D1::RectF(cardRect.left + innerPad, cardRect.top + 28.0f, cardRect.right - innerPad, cardRect.bottom - 10.0f);
    const bool isUsageSection = section.title == L"Usage HUD";

    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(isUsageSection ? D2D1::ColorF(0.11f, 0.14f, 0.18f, 1.0f) : CardFill(), &brush);
    if (!brush) {
        return 0.0f;
    }

    brush->SetOpacity(0.92f * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(cardRect, 14.0f, 14.0f), brush.Get());
    brush->SetColor(isUsageSection ? section.accent : StrokeColor());
    brush->SetOpacity((isUsageSection ? 0.32f : 0.72f) * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(cardRect, 14.0f, 14.0f), brush.Get(), 1.0f);

    ComPtr<ID2D1SolidColorBrush> accentBrush;
    ctx->CreateSolidColorBrush(section.accent, &accentBrush);
    if (accentBrush) {
        accentBrush->SetOpacity(0.92f * alpha);
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(cardRect.left + 6.0f, cardRect.top + 10.0f, cardRect.left + 9.0f, cardRect.bottom - 10.0f),
                1.5f, 1.5f),
            accentBrush.Get());
    }

    DrawTextLine(section.title, m_tinyFormat.Get(), titleRect,
        isUsageSection ? D2D1::ColorF(0.94f, 0.96f, 1.0f, 1.0f) : D2D1::ColorF(0.84f, 0.84f, 0.89f, 1.0f), alpha, false);
    DrawTextLine(section.body, m_smallFormat.Get(), bodyRect,
        D2D1::ColorF(1, 1, 1, 1), alpha, true);

    return height;
}

void AgentSessionsComponent::DrawPill(const D2D1_RECT_F& rect, const std::wstring& text, bool selected, float alpha, bool hovered) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(selected ? D2D1::ColorF(0.28f, 0.46f, 0.95f, 1.0f) : CardFill(), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity((selected ? 0.94f : (hovered ? 0.96f : 0.88f)) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), brush.Get());

    brush->SetColor(selected ? D2D1::ColorF(0.40f, 0.62f, 1.0f, 1.0f) : StrokeColor());
    brush->SetOpacity(alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), brush.Get(), hovered ? 1.3f : 1.0f);

    DrawTextLine(text, m_smallFormat.Get(), rect,
        selected ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0.92f, 0.92f, 0.96f, 1.0f),
        alpha, false, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void AgentSessionsComponent::DrawSourceGlyph(AgentKind kind, const D2D1_RECT_F& rect, float alpha) {
    if (kind == AgentKind::Claude) {
        DrawClaudeCrabIcon(rect, alpha, false);
        return;
    }
    DrawCodexPetIcon(rect, alpha, false);
}

void AgentSessionsComponent::DrawTextLine(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
    const D2D1_COLOR_F& color, float alpha, bool wrap, DWRITE_TEXT_ALIGNMENT alignment) const {
    if (!m_res || !m_res->d2dContext || !m_res->dwriteFactory || !format || text.empty()) {
        return;
    }

    const float width = (std::max)(1.0f, rect.right - rect.left);
    const float height = wrap ? 500.0f : (std::max)(1.0f, rect.bottom - rect.top);

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_res->dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, height, &layout))) {
        return;
    }

    layout->SetTextAlignment(alignment);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);

    if (!wrap) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(m_res->dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis))) {
            layout->SetTrimming(&trimming, ellipsis.Get());
        }
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    m_res->d2dContext->CreateSolidColorBrush(color, &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity(alpha);
    m_res->d2dContext->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush.Get());
}

float AgentSessionsComponent::MeasureWrappedHeight(const std::wstring& text, IDWriteTextFormat* format, float width) const {
    if (!m_res || !m_res->dwriteFactory || !format || text.empty()) {
        return 0.0f;
    }

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_res->dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, 1000.0f, &layout))) {
        return 0.0f;
    }

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return 0.0f;
    }
    return (std::max)(16.0f, metrics.height);
}

std::vector<const AgentSessionSummary*> AgentSessionsComponent::FilteredSessions() const {
    std::vector<const AgentSessionSummary*> filtered;
    filtered.reserve(m_summaries.size());
    for (const auto& summary : m_summaries) {
        if (AgentSessionMatchesFilter(summary, m_filter)) {
            filtered.push_back(&summary);
        }
    }
    return filtered;
}

const AgentSessionSummary* AgentSessionsComponent::FindSelectedSummary() const {
    for (const auto& summary : m_summaries) {
        if (summary.kind == m_selectedKind && summary.sessionId == m_selectedSessionId) {
            return &summary;
        }
    }
    return nullptr;
}

std::vector<AgentSessionsComponent::OverviewSection> AgentSessionsComponent::BuildOverviewForSelectedSession() const {
    std::vector<OverviewSection> sections;
    const AgentSessionSummary* summary = FindSelectedSummary();
    if (!summary) {
        return sections;
    }

    const AgentHistoryEntry* latestPrompt = FindLatestEntry(AgentEntryDirection::User);
    const AgentHistoryEntry* latestOutput = FindLatestOutputEntry();
    const AgentHistoryEntry* latestTool = FindLatestToolEntry();

    const std::wstring stateText =
        summary->kind == AgentKind::Claude
        ? AgentPhaseLabel(summary->phase)
        : (summary->isLive ? L"Active" : L"Idle");

    OverviewSection session;
    session.title = L"Session";
    session.body =
        L"Provider  " + AgentKindLabel(summary->kind) + L"\n" +
        L"Session ID  " + summary->sessionId + L"\n" +
        L"Project  " + ProjectLabel(summary->projectPath) + L"\n" +
        L"Last Active  " + RelativeTimeLabel(summary->lastActivityTs) + L"\n" +
        L"State  " + stateText;
    session.accent = summary->kind == AgentKind::Claude ? AccentColor(AgentKind::Claude) : AccentColor(AgentKind::Codex);
    sections.push_back(session);

    OverviewSection prompt;
    prompt.title = L"Latest Prompt";
    prompt.body = latestPrompt ? latestPrompt->text : L"No prompt captured";
    prompt.accent = D2D1::ColorF(0.40f, 0.62f, 1.0f, 1.0f);
    sections.push_back(prompt);

    OverviewSection output;
    output.title = L"Latest Output";
    output.body = latestOutput ? latestOutput->text : L"No output captured";
    output.accent = AccentColor(summary->kind);
    sections.push_back(output);

    OverviewSection tool;
    tool.title = L"Recent Tool / Activity";
    if (summary->kind == AgentKind::Claude && !summary->pendingToolName.empty()) {
        tool.body = L"Pending  " + summary->pendingToolName;
    } else if (!summary->recentActivityText.empty()) {
        tool.body = summary->recentActivityText;
    } else {
        tool.body = latestTool ? latestTool->text : L"No tool activity captured";
    }
    tool.accent = D2D1::ColorF(0.62f, 0.62f, 0.70f, 1.0f);
    sections.push_back(tool);

    if (summary->hasHudData) {
        OverviewSection usage;
        usage.title = L"Usage HUD";
        std::wstring usageBody;
        if (summary->contextUsedPercent >= 0) {
            usageBody += L"CTX  " + RemainingPercentText(summary->contextUsedPercent) + L"  " + MiniBar(summary->contextUsedPercent) + L"\n";
        }
        usageBody += L"5H  " + RemainingPercentText(summary->fiveHourUsedPercent) + L"  " + MiniBar(summary->fiveHourUsedPercent, 6) + L"\n";
        usageBody += L"7D  " + RemainingPercentText(summary->sevenDayUsedPercent) + L"  " + MiniBar(summary->sevenDayUsedPercent, 6);
        if (summary->inputTokens > 0 || summary->outputTokens > 0) {
            usageBody += L"\nInput  " + CompactCount(summary->inputTokens) + L"   Output  " + CompactCount(summary->outputTokens);
        }
        usage.body = usageBody;
        usage.accent = AccentColor(summary->kind);
        sections.push_back(usage);
    }

    return sections;
}

const AgentSessionSummary* AgentSessionsComponent::FindSummary(AgentKind kind) const {
    for (const auto& summary : m_summaries) {
        if (summary.kind == kind) {
            return &summary;
        }
    }
    return nullptr;
}

std::vector<AgentKind> AgentSessionsComponent::AvailableCompactProviders() const {
    std::vector<AgentKind> providers;
    if (FindSummary(AgentKind::Claude)) {
        providers.push_back(AgentKind::Claude);
    }
    if (FindSummary(AgentKind::Codex)) {
        providers.push_back(AgentKind::Codex);
    }
    return providers;
}

bool AgentSessionsComponent::HasAnimatedProcessingProvider() const {
    for (const auto& summary : m_summaries) {
        if (summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting) {
            return true;
        }
    }
    return false;
}

AgentKind AgentSessionsComponent::ResolveCompactProvider() const {
    if (FindSummary(m_compactProvider)) {
        return m_compactProvider;
    }
    if (FindSummary(AgentKind::Claude)) {
        return AgentKind::Claude;
    }
    return AgentKind::Codex;
}

AgentKind AgentSessionsComponent::ResolveWorkingBadgeProvider() const {
    const AgentKind preferred = ResolveCompactProvider();
    const AgentSessionSummary* preferredSummary = FindSummary(preferred);
    if (preferredSummary &&
        (preferredSummary->phase == AgentSessionPhase::Processing || preferredSummary->phase == AgentSessionPhase::Compacting)) {
        return preferred;
    }
    for (const auto& summary : m_summaries) {
        if (summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting) {
            return summary.kind;
        }
    }
    return preferred;
}

std::wstring AgentSessionsComponent::ShortSessionId(const std::wstring& sessionId) {
    if (sessionId.size() <= 8) {
        return sessionId;
    }
    return sessionId.substr(sessionId.size() - 8);
}

const AgentHistoryEntry* AgentSessionsComponent::FindLatestEntry(AgentEntryDirection direction) const {
    for (auto it = m_selectedHistory.rbegin(); it != m_selectedHistory.rend(); ++it) {
        if (it->direction == direction) {
            return &(*it);
        }
    }
    return nullptr;
}

const AgentHistoryEntry* AgentSessionsComponent::FindLatestOutputEntry() const {
    for (auto it = m_selectedHistory.rbegin(); it != m_selectedHistory.rend(); ++it) {
        if (it->direction == AgentEntryDirection::Assistant) {
            return &(*it);
        }
    }
    for (auto it = m_selectedHistory.rbegin(); it != m_selectedHistory.rend(); ++it) {
        if (it->direction == AgentEntryDirection::System) {
            return &(*it);
        }
    }
    return nullptr;
}

const AgentHistoryEntry* AgentSessionsComponent::FindLatestToolEntry() const {
    for (auto it = m_selectedHistory.rbegin(); it != m_selectedHistory.rend(); ++it) {
        if (it->direction == AgentEntryDirection::System) {
            return &(*it);
        }
    }
    return nullptr;
}

AgentSessionsComponent::ClaudeVisualPhase AgentSessionsComponent::GetClaudePrimaryPhase() const {
    bool hasClaude = false;
    ClaudeVisualPhase phase = ClaudeVisualPhase::Idle;
    for (const auto& summary : m_summaries) {
        if (summary.kind != AgentKind::Claude) {
            continue;
        }

        hasClaude = true;
        const ClaudeVisualPhase current = PhaseForSummary(summary);
        if (current == ClaudeVisualPhase::WaitingForApproval) {
            return current;
        }
        if (current == ClaudeVisualPhase::Processing) {
            phase = ClaudeVisualPhase::Processing;
        } else if (phase == ClaudeVisualPhase::Idle && current == ClaudeVisualPhase::WaitingForInput) {
            phase = ClaudeVisualPhase::WaitingForInput;
        }
    }

    return hasClaude ? phase : ClaudeVisualPhase::Idle;
}

AgentSessionsComponent::ClaudeVisualPhase AgentSessionsComponent::PhaseForSummary(const AgentSessionSummary& summary) const {
    if (summary.phase == AgentSessionPhase::WaitingForApproval) {
        return ClaudeVisualPhase::WaitingForApproval;
    }
    if (summary.phase == AgentSessionPhase::WaitingForInput) {
        return ClaudeVisualPhase::WaitingForInput;
    }
    if (summary.phase == AgentSessionPhase::Processing || summary.phase == AgentSessionPhase::Compacting) {
        return ClaudeVisualPhase::Processing;
    }
    return ClaudeVisualPhase::Idle;
}

bool AgentSessionsComponent::Contains(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

float AgentSessionsComponent::Clamp(float value, float minValue, float maxValue) {
    return (std::max)(minValue, (std::min)(maxValue, value));
}

std::wstring AgentSessionsComponent::RelativeTimeLabel(uint64_t timestampMs) {
    if (timestampMs == 0) {
        return L"unknown";
    }

    const uint64_t now = CurrentUnixMs();
    if (timestampMs >= now || now - timestampMs < 1000) {
        return L"now";
    }

    const uint64_t seconds = (now - timestampMs) / 1000ULL;
    if (seconds < 60ULL) {
        return std::to_wstring(seconds) + L"s";
    }

    const uint64_t minutes = seconds / 60ULL;
    if (minutes < 60ULL) {
        return std::to_wstring(minutes) + L"m";
    }

    const uint64_t hours = minutes / 60ULL;
    if (hours < 24ULL) {
        return std::to_wstring(hours) + L"h";
    }

    return std::to_wstring(hours / 24ULL) + L"d";
}

std::wstring AgentSessionsComponent::ProjectLabel(const std::wstring& projectPath) {
    if (projectPath.empty()) {
        return L"No project";
    }

    const size_t slash = projectPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash + 1 >= projectPath.size()) {
        return projectPath;
    }

    return projectPath.substr(slash + 1);
}

D2D1_COLOR_F AgentSessionsComponent::SurfaceFill() const {
    return m_darkMode
        ? D2D1::ColorF(0.10f, 0.10f, 0.12f, 1.0f)
        : D2D1::ColorF(0.95f, 0.95f, 0.97f, 1.0f);
}

D2D1_COLOR_F AgentSessionsComponent::CardFill() const {
    return m_darkMode
        ? D2D1::ColorF(0.15f, 0.15f, 0.18f, 1.0f)
        : D2D1::ColorF(0.98f, 0.98f, 0.99f, 1.0f);
}

D2D1_COLOR_F AgentSessionsComponent::StrokeColor() const {
    return m_darkMode
        ? D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f)
        : D2D1::ColorF(0.80f, 0.80f, 0.84f, 1.0f);
}

D2D1_COLOR_F AgentSessionsComponent::AccentColor(AgentKind kind) const {
    if (kind == AgentKind::Claude) {
        return D2D1::ColorF(kClaudePromptR, kClaudePromptG, kClaudePromptB, 1.0f);
    }
    return D2D1::ColorF(0.20f, 0.64f, 0.98f, 1.0f);
}

D2D1_COLOR_F AgentSessionsComponent::PhaseColor(ClaudeVisualPhase phase) const {
    switch (phase) {
    case ClaudeVisualPhase::Processing:
        return D2D1::ColorF(kClaudePromptR, kClaudePromptG, kClaudePromptB, 1.0f);
    case ClaudeVisualPhase::WaitingForApproval:
        return D2D1::ColorF(1.0f, 0.70f, 0.0f, 1.0f);
    case ClaudeVisualPhase::WaitingForInput:
        return D2D1::ColorF(0.40f, 0.75f, 0.45f, 1.0f);
    case ClaudeVisualPhase::Idle:
    default:
        return D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f);
    }
}

AgentSessionsComponent::HitTarget AgentSessionsComponent::HitTest(float x, float y) const {
    for (const auto& hit : m_hits) {
        if (Contains(hit.rect, x, y)) {
            return hit;
        }
    }
    if (m_mode == IslandDisplayMode::AgentCompact && Contains(m_lastRect, x, y)) {
        return { m_compactChooserOpen ? HitKind::CompactDismissChooser : HitKind::CompactExpandBody, ResolveCompactProvider(), L"", m_lastRect };
    }
    return {};
}
