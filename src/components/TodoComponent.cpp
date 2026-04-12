#include "components/TodoComponent.h"

#include <algorithm>
#include <cmath>
#include <imm.h>

#pragma comment(lib, "imm32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr float kCompactBadgeRadius = 12.0f;
constexpr float kExpandedCorner = 16.0f;
constexpr float kRowHeight = 54.0f;
constexpr float kScrollStep = 28.0f;
constexpr float kLaunchDuration = 0.32f;

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
}

TodoComponent::TodoComponent() = default;

void TodoComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void TodoComponent::Update(float deltaTime) {
    if (!m_launchAnimating) {
        return;
    }

    m_launchElapsed += deltaTime;
    if (m_launchElapsed >= kLaunchDuration) {
        m_launchAnimating = false;
        m_launchElapsed = 0.0f;
        if (m_onLaunchComplete) {
            m_onLaunchComplete();
        }
    }
}

bool TodoComponent::IsActive() const {
    return m_mode == IslandDisplayMode::TodoInputCompact ||
        m_mode == IslandDisplayMode::TodoListCompact ||
        m_mode == IslandDisplayMode::TodoExpanded;
}

bool TodoComponent::NeedsRender() const {
    return m_launchAnimating;
}

float TodoComponent::LaunchProgress() const {
    if (!m_launchAnimating) {
        return 0.0f;
    }
    return Clamp01(m_launchElapsed / kLaunchDuration);
}

void TodoComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    (void)currentTimeMs;
    if (!m_res || !m_res->d2dContext || !IsActive() || contentAlpha <= 0.01f) {
        return;
    }

    m_lastRect = rect;
    if (m_mode == IslandDisplayMode::TodoInputCompact) {
        DrawCompactInput(rect, contentAlpha);
    } else if (m_mode == IslandDisplayMode::TodoListCompact) {
        DrawCompactList(rect, contentAlpha);
    } else if (m_mode == IslandDisplayMode::TodoExpanded) {
        DrawExpanded(rect, contentAlpha);
    }
}

void TodoComponent::DrawIdleBadge(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) {
    (void)currentTimeMs;
    m_idleBadgeRect = rect;
    if (!m_res || !m_res->d2dContext || contentAlpha <= 0.01f) {
        return;
    }
    DrawIdleBadgeContent(rect, contentAlpha);
}

void TodoComponent::DrawIdleLaunchOverlay(const D2D1_RECT_F& contentRect, const D2D1_RECT_F& badgeRect, float contentAlpha, ULONGLONG currentTimeMs) {
    (void)currentTimeMs;
    m_idleBadgeRect = badgeRect;
    if (!m_res || !m_res->d2dContext || contentAlpha <= 0.01f || !m_launchAnimating) {
        return;
    }
    DrawIdleLaunchAnimation(contentRect, badgeRect, contentAlpha);
}

bool TodoComponent::OnIdleBadgeMouseMove(float x, float y) {
    const bool hovered = IdleBadgeContains(x, y);
    const bool changed = hovered != m_idleBadgeHovered;
    m_idleBadgeHovered = hovered;
    return changed;
}

bool TodoComponent::IdleBadgeContains(float x, float y) const {
    return Contains(m_idleBadgeRect, x, y);
}

void TodoComponent::BeginCompactInputFocus() {
    SetActiveField(ActiveField::CompactInput);
}

bool TodoComponent::BeginIdleOpenAnimation() {
    if (m_launchAnimating) {
        return false;
    }

    m_launchAnimating = true;
    m_launchElapsed = 0.0f;
    return true;
}

bool TodoComponent::OnMouseWheel(float x, float y, int delta) {
    (void)x;
    (void)y;
    if (m_mode != IslandDisplayMode::TodoExpanded || m_maxScroll <= 0.0f) {
        return false;
    }

    m_listScroll -= (delta > 0 ? kScrollStep : -kScrollStep);
    m_listScroll = (std::max)(0.0f, (std::min)(m_maxScroll, m_listScroll));
    return true;
}

bool TodoComponent::OnMouseMove(float x, float y) {
    HitKind previousKind = m_hoveredKind;
    uint64_t previousItem = m_hoveredItemId;

    m_hoveredKind = HitKind::None;
    m_hoveredItemId = 0;

    if (m_mode == IslandDisplayMode::TodoListCompact) {
        if (Contains(m_lastRect, x, y)) {
            m_hoveredKind = HitKind::CompactBody;
        }
    } else if (m_mode == IslandDisplayMode::TodoExpanded && Contains(m_lastRect, x, y)) {
        HitTarget hit = HitTestExpanded(x, y);
        m_hoveredKind = hit.kind;
        m_hoveredItemId = hit.itemId;
    }

    return previousKind != m_hoveredKind || previousItem != m_hoveredItemId;
}

bool TodoComponent::OnMouseClick(float x, float y) {
    if (m_mode == IslandDisplayMode::TodoInputCompact) {
        if (!Contains(m_lastRect, x, y)) {
            return false;
        }
        SetActiveField(ActiveField::CompactInput);
        return true;
    }

    if (m_mode == IslandDisplayMode::TodoListCompact) {
        return Contains(m_lastRect, x, y);
    }

    if (m_mode != IslandDisplayMode::TodoExpanded || !Contains(m_lastRect, x, y)) {
        return false;
    }

    const HitTarget hit = HitTestExpanded(x, y);
    switch (hit.kind) {
    case HitKind::HeaderClose:
        if (m_onRequestCloseExpanded) {
            m_onRequestCloseExpanded();
        }
        return true;
    case HitKind::TitleField:
        SetActiveField(ActiveField::EditorTitle);
        return true;
    case HitKind::NoteField:
        SetActiveField(ActiveField::EditorNote);
        return true;
    case HitKind::PriorityHigh:
        m_editorPriority = TodoPriority::High;
        return true;
    case HitKind::PriorityMedium:
        m_editorPriority = TodoPriority::Medium;
        return true;
    case HitKind::PriorityLow:
        m_editorPriority = TodoPriority::Low;
        return true;
    case HitKind::SaveButton:
        SaveEditor();
        return true;
    case HitKind::CancelButton:
        ResetExpandedEditor();
        return true;
    case HitKind::RowToggle:
        if (m_store) {
            if (const TodoItem* item = m_store->FindItem(hit.itemId)) {
                m_store->SetCompleted(hit.itemId, !item->completed);
            }
        }
        return true;
    case HitKind::RowEdit:
        BeginEdit(hit.itemId);
        return true;
    case HitKind::RowDelete:
        if (m_store) {
            m_store->RemoveItem(hit.itemId);
            if (m_editingItemId == hit.itemId) {
                ResetExpandedEditor();
            }
        }
        return true;
    case HitKind::None:
    default:
        SetActiveField(ActiveField::None);
        return true;
    }
}

bool TodoComponent::OnChar(wchar_t ch) {
    if (m_activeField == ActiveField::None) {
        return false;
    }

    if (ch == L'\b') {
        DeleteBackward();
        return true;
    }

    if (ch >= 0x20 && ch != 0x7F) {
        InsertText(std::wstring(1, ch));
        return true;
    }

    return false;
}

bool TodoComponent::OnKeyDown(WPARAM key) {
    if (m_mode == IslandDisplayMode::TodoInputCompact) {
        switch (key) {
        case VK_ESCAPE: CancelCompactInput(); return true;
        case VK_RETURN: SubmitCompactInput(); return true;
        case VK_LEFT: MoveCursorLeft(); return true;
        case VK_RIGHT: MoveCursorRight(); return true;
        case VK_HOME: MoveCursorHome(); return true;
        case VK_END: MoveCursorEnd(); return true;
        case VK_DELETE: DeleteForward(); return true;
        default: return false;
        }
    }

    if (m_mode == IslandDisplayMode::TodoExpanded) {
        switch (key) {
        case VK_ESCAPE:
            if (m_activeField != ActiveField::None) SetActiveField(ActiveField::None);
            else if (!m_editorTitle.empty() || !m_editorNote.empty() || m_editingItemId != 0) ResetExpandedEditor();
            else if (m_onRequestCloseExpanded) m_onRequestCloseExpanded();
            return true;
        case VK_RETURN: SaveEditor(); return true;
        case VK_TAB:
            if (m_activeField == ActiveField::EditorTitle) SetActiveField(ActiveField::EditorNote);
            else SetActiveField(ActiveField::EditorTitle);
            return true;
        case VK_LEFT: MoveCursorLeft(); return true;
        case VK_RIGHT: MoveCursorRight(); return true;
        case VK_HOME: MoveCursorHome(); return true;
        case VK_END: MoveCursorEnd(); return true;
        case VK_DELETE: DeleteForward(); return true;
        default: return false;
        }
    }

    return false;
}

bool TodoComponent::OnImeComposition(HWND hwnd, LPARAM lParam) {
    if (m_activeField == ActiveField::None || !(lParam & GCS_RESULTSTR)) {
        return false;
    }

    HIMC himc = ImmGetContext(hwnd);
    if (!himc) {
        return false;
    }

    LONG byteLen = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
    if (byteLen > 0) {
        std::wstring result(byteLen / sizeof(wchar_t), L'\0');
        ImmGetCompositionStringW(himc, GCS_RESULTSTR, result.data(), byteLen);
        InsertText(result);
    }

    ImmReleaseContext(hwnd, himc);
    return byteLen > 0;
}

bool TodoComponent::OnImeSetContext(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (m_activeField == ActiveField::None) {
        return false;
    }

    const LPARAM masked = lParam & ~ISC_SHOWUICOMPOSITIONWINDOW;
    result = DefWindowProcW(hwnd, WM_IME_SETCONTEXT, wParam, masked);
    return true;
}

D2D1_RECT_F TodoComponent::GetImeAnchorRect() const {
    switch (m_activeField) {
    case ActiveField::CompactInput: return m_compactInputRect;
    case ActiveField::EditorTitle: return m_titleFieldRect;
    case ActiveField::EditorNote: return m_noteFieldRect;
    case ActiveField::None:
    default:
        return D2D1::RectF(0, 0, 0, 0);
    }
}

void TodoComponent::DrawIdleBadgeContent(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    const D2D1_COLOR_F accent = (m_store && m_store->GetTopIncomplete())
        ? PriorityColor(m_store->GetTopIncomplete()->priority)
        : D2D1::ColorF(0.55f, 0.55f, 0.60f, 1.0f);

    brush->SetOpacity((m_idleBadgeHovered ? 0.98f : 0.88f) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, kCompactBadgeRadius, kCompactBadgeRadius), brush.Get());

    brush->SetColor(accent);
    brush->SetOpacity(alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, kCompactBadgeRadius, kCompactBadgeRadius), brush.Get(), m_idleBadgeHovered ? 1.6f : 1.2f);

    const D2D1_RECT_F iconRect = D2D1::RectF(rect.left + 6.0f, rect.top + 4.0f, rect.left + 22.0f, rect.bottom - 4.0f);
    DrawTodoIcon(iconRect, accent, alpha, true);

    const size_t incomplete = m_store ? m_store->CountIncomplete() : 0;
    if (incomplete > 0) {
        const std::wstring countText = incomplete > 9 ? L"9+" : std::to_wstring(incomplete);
        const D2D1_RECT_F badgeRect = D2D1::RectF(rect.right - 13.0f, rect.top + 2.0f, rect.right - 2.0f, rect.top + 13.0f);
        brush->SetColor(accent);
        brush->SetOpacity(alpha);
        ctx->FillEllipse(D2D1::Ellipse(
            D2D1::Point2F((badgeRect.left + badgeRect.right) * 0.5f, (badgeRect.top + badgeRect.bottom) * 0.5f),
            (badgeRect.right - badgeRect.left) * 0.5f,
            (badgeRect.bottom - badgeRect.top) * 0.5f), brush.Get());
        DrawText(countText, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, badgeRect,
            D2D1::ColorF(1, 1, 1, 1), alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void TodoComponent::DrawIdleLaunchAnimation(const D2D1_RECT_F& contentRect, const D2D1_RECT_F& badgeRect, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    const D2D1_COLOR_F accent = (m_store && m_store->GetTopIncomplete())
        ? PriorityColor(m_store->GetTopIncomplete()->priority)
        : D2D1::ColorF(0.55f, 0.55f, 0.60f, 1.0f);

    const float t = EaseOutCubic(m_launchElapsed / kLaunchDuration);
    const float fade = 1.0f - Clamp01((t - 0.78f) / 0.22f);
    const float inputLeft = contentRect.left + 10.0f;
    const D2D1_RECT_F fullInputRect = D2D1::RectF(inputLeft, contentRect.top + 6.0f, contentRect.right - 10.0f, contentRect.bottom - 6.0f);
    const float iconStartX = badgeRect.left + 6.0f;
    const float iconEndX = fullInputRect.right - 22.0f;
    const float iconX = iconStartX + (iconEndX - iconStartX) * t;
    const float revealRight = (std::min)(fullInputRect.right, iconX + 20.0f);
    const float revealWidth = (std::max)(0.0f, revealRight - fullInputRect.left);

    if (revealWidth > 1.0f) {
        D2D1_RECT_F revealClip = D2D1::RectF(fullInputRect.left, fullInputRect.top, revealRight, fullInputRect.bottom);
        ctx->PushAxisAlignedClip(revealClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        brush->SetColor(SecondaryFill());
        brush->SetOpacity((0.22f + 0.74f * t) * alpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(fullInputRect, 12.0f, 12.0f), brush.Get());

        if (revealWidth > 42.0f) {
            const D2D1_RECT_F placeholderRect = D2D1::RectF(fullInputRect.left + 12.0f, fullInputRect.top + 4.0f, fullInputRect.right - 12.0f, fullInputRect.bottom - 4.0f);
            DrawText(L"Add TODO...", m_res->subFormat ? m_res->subFormat : m_res->titleFormat, placeholderRect,
                D2D1::ColorF(0.68f, 0.68f, 0.74f, 1.0f), alpha * Clamp01((t - 0.28f) / 0.72f), DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        const float caretAlpha = alpha * Clamp01((t - 0.18f) / 0.82f);
        if (caretAlpha > 0.01f) {
            brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
            brush->SetOpacity(caretAlpha);
            const float caretX = fullInputRect.left + 12.0f;
            ctx->DrawLine(
                D2D1::Point2F(caretX, fullInputRect.top + 8.0f),
                D2D1::Point2F(caretX, fullInputRect.bottom - 8.0f),
                brush.Get(),
                1.2f);
        }

        ctx->PopAxisAlignedClip();
    }

    const float lift = -2.0f * t;
    D2D1_RECT_F iconRect = D2D1::RectF(iconX, badgeRect.top + 4.0f + lift, iconX + 16.0f, badgeRect.bottom - 4.0f + lift);
    DrawTodoIcon(iconRect, accent, alpha * fade, true);
}

void TodoComponent::DrawCompactInput(const D2D1_RECT_F& rect, float alpha) {
    m_compactInputRect = D2D1::RectF(rect.left + 10.0f, rect.top + 6.0f, rect.right - 10.0f, rect.bottom - 6.0f);
    DrawField(m_compactInputRect, m_compactInputText, L"Add TODO...", m_activeField == ActiveField::CompactInput, alpha);
}

void TodoComponent::DrawCompactList(const D2D1_RECT_F& rect, float alpha) {
    const D2D1_RECT_F card = D2D1::RectF(rect.left + 10.0f, rect.top + 6.0f, rect.right - 10.0f, rect.bottom - 6.0f);
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity((m_hoveredKind == HitKind::CompactBody ? 0.97f : 0.92f) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(card, kCompactBadgeRadius, kCompactBadgeRadius), brush.Get());

    const D2D1_COLOR_F accent = (m_store && m_store->GetTopIncomplete())
        ? PriorityColor(m_store->GetTopIncomplete()->priority)
        : D2D1::ColorF(0.52f, 0.55f, 0.60f, 1.0f);
    brush->SetColor(accent);
    brush->SetOpacity(alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(card, kCompactBadgeRadius, kCompactBadgeRadius), brush.Get(), m_hoveredKind == HitKind::CompactBody ? 1.7f : 1.2f);

    brush->SetOpacity(0.90f * alpha);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(card.left + 13.0f, (card.top + card.bottom) * 0.5f), 3.0f, 3.0f), brush.Get());

    const D2D1_RECT_F iconRect = D2D1::RectF(card.left + 17.0f, card.top + 3.0f, card.left + 33.0f, card.bottom - 3.0f);
    DrawTodoIcon(iconRect, accent, alpha, true);

    const D2D1_RECT_F countRect = D2D1::RectF(card.right - 34.0f, card.top + 2.0f, card.right - 8.0f, card.bottom - 2.0f);
    const std::wstring countText = m_store ? std::to_wstring(m_store->CountIncomplete()) : L"0";
    DrawText(countText, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, countRect,
        D2D1::ColorF(1, 1, 1, 1), alpha, DWRITE_TEXT_ALIGNMENT_CENTER);

    const D2D1_RECT_F labelRect = D2D1::RectF(card.left + 38.0f, card.top + 2.0f, card.left + 78.0f, card.bottom - 2.0f);
    DrawText(L"TODO", m_res->subFormat ? m_res->subFormat : m_res->titleFormat, labelRect,
        D2D1::ColorF(0.80f, 0.80f, 0.84f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);

    const std::wstring summary = SummaryText();
    if (!summary.empty()) {
        const D2D1_RECT_F textRect = D2D1::RectF(card.left + 76.0f, card.top + 2.0f, countRect.left - 6.0f, card.bottom - 2.0f);
        DrawText(summary, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, textRect,
            D2D1::ColorF(1, 1, 1, 1), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void TodoComponent::DrawExpanded(const D2D1_RECT_F& rect, float alpha) {
    auto* ctx = m_res->d2dContext;
    m_hits.clear();

    const float outerPad = 14.0f;
    const D2D1_RECT_F surfaceRect = D2D1::RectF(rect.left + outerPad, rect.top + 8.0f, rect.right - outerPad, rect.bottom - 10.0f);
    const D2D1_RECT_F heroCard = D2D1::RectF(surfaceRect.left + 4.0f, surfaceRect.top + 4.0f, surfaceRect.right - 4.0f, surfaceRect.top + 62.0f);
    const D2D1_RECT_F headerRect = D2D1::RectF(heroCard.left + 16.0f, heroCard.top + 10.0f, heroCard.right - 86.0f, heroCard.top + 34.0f);
    m_closeRect = D2D1::RectF(heroCard.right - 38.0f, heroCard.top + 10.0f, heroCard.right - 10.0f, heroCard.top + 36.0f);

    const D2D1_RECT_F editorCard = D2D1::RectF(surfaceRect.left + 4.0f, heroCard.bottom + 10.0f, surfaceRect.right - 4.0f, heroCard.bottom + 144.0f);
    const D2D1_RECT_F listCard = D2D1::RectF(surfaceRect.left + 4.0f, editorCard.bottom + 10.0f, surfaceRect.right - 4.0f, surfaceRect.bottom - 4.0f);
    m_listViewportRect = D2D1::RectF(listCard.left + 10.0f, listCard.top + 12.0f, listCard.right - 10.0f, listCard.bottom - 12.0f);

    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity(0.94f * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(surfaceRect, kExpandedCorner + 4.0f, kExpandedCorner + 4.0f), brush.Get());

    brush->SetColor(StrokeColor());
    brush->SetOpacity(0.85f * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(surfaceRect, kExpandedCorner + 4.0f, kExpandedCorner + 4.0f), brush.Get(), 1.0f);

    brush->SetColor(m_darkMode ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.045f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.60f));
    brush->SetOpacity(alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(heroCard, kExpandedCorner + 2.0f, kExpandedCorner + 2.0f), brush.Get());

    brush->SetColor(m_darkMode ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.030f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.52f));
    ctx->FillRoundedRectangle(D2D1::RoundedRect(editorCard, kExpandedCorner, kExpandedCorner), brush.Get());
    brush->SetColor(m_darkMode ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.022f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.46f));
    ctx->FillRoundedRectangle(D2D1::RoundedRect(listCard, kExpandedCorner, kExpandedCorner), brush.Get());

    brush->SetColor(StrokeColor());
    brush->SetOpacity(0.55f * alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(heroCard, kExpandedCorner + 2.0f, kExpandedCorner + 2.0f), brush.Get(), 1.0f);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(editorCard, kExpandedCorner, kExpandedCorner), brush.Get(), 1.0f);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(listCard, kExpandedCorner, kExpandedCorner), brush.Get(), 1.0f);

    DrawText(L"TODO", m_res->titleFormat, headerRect, D2D1::ColorF(1, 1, 1, 1), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);
    const std::wstring stats = std::to_wstring(m_store ? m_store->CountIncomplete() : 0) + L" open";
    const std::wstring total = std::to_wstring(m_store ? m_store->Items().size() : 0) + L" total";
    const D2D1_RECT_F statsRect = D2D1::RectF(headerRect.left, heroCard.top + 30.0f, heroCard.right - 120.0f, heroCard.bottom - 10.0f);
    DrawText(stats, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, statsRect,
        D2D1::ColorF(0.92f, 0.92f, 0.96f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);
    const D2D1_RECT_F totalRect = D2D1::RectF(heroCard.right - 128.0f, heroCard.bottom - 30.0f, heroCard.right - 18.0f, heroCard.bottom - 8.0f);
    DrawText(total, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, totalRect,
        D2D1::ColorF(0.76f, 0.76f, 0.80f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawButton(m_closeRect, L"#close", false, alpha, m_hoveredKind == HitKind::HeaderClose);
    m_hits.push_back({ HitKind::HeaderClose, 0, m_closeRect });

    const D2D1_RECT_F sectionLabelRect = D2D1::RectF(editorCard.left + 14.0f, editorCard.top + 10.0f, editorCard.left + 180.0f, editorCard.top + 30.0f);
    DrawText(m_editingItemId == 0 ? L"Quick Add" : L"Edit Item", m_res->subFormat ? m_res->subFormat : m_res->titleFormat, sectionLabelRect,
        D2D1::ColorF(0.78f, 0.78f, 0.82f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);

    m_titleFieldRect = D2D1::RectF(editorCard.left + 14.0f, editorCard.top + 38.0f, editorCard.right - 144.0f, editorCard.top + 74.0f);
    m_noteFieldRect = D2D1::RectF(editorCard.left + 14.0f, editorCard.top + 84.0f, editorCard.right - 144.0f, editorCard.top + 116.0f);
    const D2D1_RECT_F saveRect = D2D1::RectF(editorCard.right - 118.0f, editorCard.top + 38.0f, editorCard.right - 14.0f, editorCard.top + 74.0f);
    const D2D1_RECT_F cancelRect = D2D1::RectF(editorCard.right - 118.0f, editorCard.top + 84.0f, editorCard.right - 14.0f, editorCard.top + 116.0f);
    const D2D1_RECT_F priorityLabelRect = D2D1::RectF(editorCard.left + 14.0f, editorCard.top + 120.0f, editorCard.left + 120.0f, editorCard.top + 138.0f);
    DrawText(L"Priority", m_res->subFormat ? m_res->subFormat : m_res->titleFormat, priorityLabelRect,
        D2D1::ColorF(0.74f, 0.74f, 0.78f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_LEADING);
    const D2D1_RECT_F highRect = D2D1::RectF(editorCard.left + 14.0f, editorCard.top + 144.0f, editorCard.left + 78.0f, editorCard.top + 172.0f);
    const D2D1_RECT_F mediumRect = D2D1::RectF(highRect.right + 10.0f, highRect.top, highRect.right + 74.0f, highRect.bottom);
    const D2D1_RECT_F lowRect = D2D1::RectF(mediumRect.right + 10.0f, highRect.top, mediumRect.right + 64.0f, highRect.bottom);

    DrawField(m_titleFieldRect, m_editorTitle, L"Title", m_activeField == ActiveField::EditorTitle, alpha);
    DrawField(m_noteFieldRect, m_editorNote, L"Note", m_activeField == ActiveField::EditorNote, alpha);
    DrawButton(saveRect, m_editingItemId == 0 ? L"Add" : L"Save", true, alpha, m_hoveredKind == HitKind::SaveButton);
    DrawButton(cancelRect, L"Cancel", false, alpha, m_hoveredKind == HitKind::CancelButton);
    DrawPriorityPill(highRect, TodoPriority::High, m_editorPriority == TodoPriority::High, alpha, m_hoveredKind == HitKind::PriorityHigh);
    DrawPriorityPill(mediumRect, TodoPriority::Medium, m_editorPriority == TodoPriority::Medium, alpha, m_hoveredKind == HitKind::PriorityMedium);
    DrawPriorityPill(lowRect, TodoPriority::Low, m_editorPriority == TodoPriority::Low, alpha, m_hoveredKind == HitKind::PriorityLow);

    m_hits.push_back({ HitKind::TitleField, 0, m_titleFieldRect });
    m_hits.push_back({ HitKind::NoteField, 0, m_noteFieldRect });
    m_hits.push_back({ HitKind::SaveButton, 0, saveRect });
    m_hits.push_back({ HitKind::CancelButton, 0, cancelRect });
    m_hits.push_back({ HitKind::PriorityHigh, 0, highRect });
    m_hits.push_back({ HitKind::PriorityMedium, 0, mediumRect });
    m_hits.push_back({ HitKind::PriorityLow, 0, lowRect });

    const auto sortedItems = m_store ? m_store->GetSortedItems() : std::vector<const TodoItem*>{};
    const float contentHeight = static_cast<float>(sortedItems.size()) * kRowHeight;
    const float viewportHeight = (std::max)(0.0f, m_listViewportRect.bottom - m_listViewportRect.top);
    m_maxScroll = (std::max)(0.0f, contentHeight - viewportHeight);
    m_listScroll = (std::max)(0.0f, (std::min)(m_maxScroll, m_listScroll));

    ctx->PushAxisAlignedClip(m_listViewportRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (sortedItems.empty()) {
        const D2D1_RECT_F emptyTitle = D2D1::RectF(m_listViewportRect.left, m_listViewportRect.top + 28.0f, m_listViewportRect.right, m_listViewportRect.top + 62.0f);
        const D2D1_RECT_F emptySub = D2D1::RectF(m_listViewportRect.left + 28.0f, emptyTitle.bottom + 10.0f, m_listViewportRect.right - 28.0f, emptyTitle.bottom + 48.0f);
        DrawText(L"No TODO items yet", m_res->titleFormat, emptyTitle,
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawText(L"Use the compact input or add one here.", m_res->subFormat ? m_res->subFormat : m_res->titleFormat, emptySub,
            D2D1::ColorF(0.72f, 0.72f, 0.76f, 1.0f), alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        for (size_t i = 0; i < sortedItems.size(); ++i) {
            const float rowTop = m_listViewportRect.top + static_cast<float>(i) * kRowHeight - m_listScroll;
            const D2D1_RECT_F rowRect = D2D1::RectF(m_listViewportRect.left, rowTop, m_listViewportRect.right, rowTop + kRowHeight - 6.0f);
            if (rowRect.bottom < m_listViewportRect.top || rowRect.top > m_listViewportRect.bottom) {
                continue;
            }
            DrawRow(rowRect, *sortedItems[i], alpha, m_hoveredItemId == sortedItems[i]->id);
        }
    }
    ctx->PopAxisAlignedClip();
}

void TodoComponent::DrawField(const D2D1_RECT_F& rect, const std::wstring& value, const std::wstring& placeholder, bool focused, float alpha) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity((focused ? 0.99f : 0.92f) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, 10.0f, 10.0f), brush.Get());

    const bool empty = value.empty();
    const std::wstring display = empty ? placeholder : value;
    const D2D1_COLOR_F textColor = empty ? D2D1::ColorF(0.60f, 0.60f, 0.64f, 1.0f) : D2D1::ColorF(1, 1, 1, 1);
    const D2D1_RECT_F textRect = D2D1::RectF(rect.left + 12.0f, rect.top + 4.0f, rect.right - 12.0f, rect.bottom - 4.0f);
    DrawText(display, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, textRect, textColor, alpha);

    if (!focused) {
        return;
    }

    const std::wstring* buffer = nullptr;
    int cursor = 0;
    if (m_activeField == ActiveField::CompactInput) {
        buffer = &m_compactInputText;
        cursor = m_compactCursor;
    } else if (m_activeField == ActiveField::EditorTitle) {
        buffer = &m_editorTitle;
        cursor = m_editorTitleCursor;
    } else if (m_activeField == ActiveField::EditorNote) {
        buffer = &m_editorNote;
        cursor = m_editorNoteCursor;
    }

    float caretX = textRect.left;
    if (buffer) {
        const std::wstring prefix = buffer->substr(0, (std::min)(cursor, static_cast<int>(buffer->size())));
        ComPtr<IDWriteTextLayout> layout;
        m_res->dwriteFactory->CreateTextLayout(prefix.c_str(), static_cast<UINT32>(prefix.size()),
            m_res->subFormat ? m_res->subFormat : m_res->titleFormat, 500.0f, 32.0f, &layout);
        if (layout) {
            DWRITE_TEXT_METRICS metrics{};
            layout->GetMetrics(&metrics);
            caretX += metrics.widthIncludingTrailingWhitespace;
        }
    }

    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    brush->SetOpacity(alpha);
    ctx->DrawLine(D2D1::Point2F(caretX, rect.top + 8.0f), D2D1::Point2F(caretX, rect.bottom - 8.0f), brush.Get(), 1.2f);
}

void TodoComponent::DrawButton(const D2D1_RECT_F& rect, const std::wstring& text, bool primary, float alpha, bool hovered) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(primary ? D2D1::ColorF(0.30f, 0.49f, 1.0f, 1.0f) : SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity((primary ? (hovered ? 1.0f : 0.97f) : (hovered ? 0.96f : 0.90f)) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), brush.Get());

    if (!primary) {
        brush->SetColor(hovered ? D2D1::ColorF(0.30f, 0.49f, 1.0f, 1.0f) : StrokeColor());
        brush->SetOpacity(alpha);
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), brush.Get(), hovered ? 1.4f : 1.0f);
    }

    if (text == L"#close") {
        DrawCloseIcon(rect, alpha);
        return;
    }
    if (text == L"#edit") {
        DrawEditIcon(rect, alpha);
        return;
    }
    if (text == L"#delete") {
        DrawDeleteIcon(rect, alpha);
        return;
    }

    IDWriteTextFormat* format = (text.size() == 1 && m_res->iconFormat) ? m_res->iconFormat : (m_res->subFormat ? m_res->subFormat : m_res->titleFormat);
    DrawText(text, format, rect, D2D1::ColorF(1, 1, 1, 1), alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void TodoComponent::DrawPriorityPill(const D2D1_RECT_F& rect, TodoPriority priority, bool selected, float alpha, bool hovered) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(PriorityColor(priority), &brush);
    if (!brush) {
        return;
    }

    brush->SetOpacity((selected ? 0.88f : (hovered ? 0.30f : 0.18f)) * alpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), brush.Get());
    brush->SetOpacity(alpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, 12.0f, 12.0f), brush.Get(), selected ? 1.5f : (hovered ? 1.2f : 1.0f));

    DrawText(TodoPriorityLabel(priority), m_res->subFormat ? m_res->subFormat : m_res->titleFormat, rect,
        selected ? D2D1::ColorF(1, 1, 1, 1) : PriorityColor(priority), alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void TodoComponent::DrawRow(const D2D1_RECT_F& rowRect, const TodoItem& item, float alpha, bool hovered) {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(SecondaryFill(), &brush);
    if (!brush) {
        return;
    }

    const float rowAlpha = item.completed ? alpha * 0.56f : alpha;
    brush->SetOpacity((hovered ? 0.90f : 0.80f) * rowAlpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(rowRect, 14.0f, 14.0f), brush.Get());

    brush->SetColor(hovered ? D2D1::ColorF(0.30f, 0.49f, 1.0f, 0.85f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, m_darkMode ? 0.08f : 0.18f));
    brush->SetOpacity(rowAlpha);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rowRect, 14.0f, 14.0f), brush.Get(), hovered ? 1.2f : 0.9f);

    const D2D1_RECT_F toggleRect = D2D1::RectF(rowRect.left + 14.0f, rowRect.top + 18.0f, rowRect.left + 30.0f, rowRect.top + 34.0f);
    const D2D1_RECT_F editRect = D2D1::RectF(rowRect.right - 116.0f, rowRect.top + 14.0f, rowRect.right - 68.0f, rowRect.top + 38.0f);
    const D2D1_RECT_F deleteRect = D2D1::RectF(rowRect.right - 60.0f, rowRect.top + 14.0f, rowRect.right - 12.0f, rowRect.top + 38.0f);
    const D2D1_RECT_F priorityBar = D2D1::RectF(rowRect.left + 40.0f, rowRect.top + 12.0f, rowRect.left + 44.0f, rowRect.bottom - 12.0f);
    const D2D1_RECT_F titleRect = D2D1::RectF(priorityBar.right + 12.0f, rowRect.top + 10.0f, editRect.left - 12.0f, rowRect.top + 28.0f);
    const D2D1_RECT_F noteRect = D2D1::RectF(priorityBar.right + 12.0f, rowRect.top + 28.0f, editRect.left - 12.0f, rowRect.bottom - 10.0f);

    brush->SetColor(item.completed ? D2D1::ColorF(0.45f, 0.80f, 0.55f, 1.0f) : PriorityColor(item.priority));
    brush->SetOpacity(rowAlpha);
    ctx->FillRectangle(priorityBar, brush.Get());
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(toggleRect, 4.0f, 4.0f), brush.Get(), 1.2f);
    if (item.completed) {
        ctx->DrawLine(D2D1::Point2F(toggleRect.left + 3.0f, toggleRect.top + 8.0f), D2D1::Point2F(toggleRect.left + 7.0f, toggleRect.bottom - 4.0f), brush.Get(), 1.4f);
        ctx->DrawLine(D2D1::Point2F(toggleRect.left + 7.0f, toggleRect.bottom - 4.0f), D2D1::Point2F(toggleRect.right - 3.0f, toggleRect.top + 4.0f), brush.Get(), 1.4f);
    }

    DrawText(item.title, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, titleRect,
        item.completed ? D2D1::ColorF(0.82f, 0.82f, 0.86f, 1.0f) : D2D1::ColorF(1, 1, 1, 1), rowAlpha);
    DrawText(item.note.empty() ? TodoPriorityLabel(item.priority) : item.note, m_res->subFormat ? m_res->subFormat : m_res->titleFormat, noteRect,
        item.completed ? D2D1::ColorF(0.58f, 0.62f, 0.66f, 1.0f) : D2D1::ColorF(0.72f, 0.72f, 0.76f, 1.0f), rowAlpha);
    if (item.completed) {
        ComPtr<ID2D1SolidColorBrush> strikeBrush;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.76f, 0.80f, 0.84f, rowAlpha * 0.75f), &strikeBrush);
        if (strikeBrush) {
            const float strikeY = titleRect.top + 12.0f;
            ctx->DrawLine(D2D1::Point2F(titleRect.left, strikeY), D2D1::Point2F(titleRect.right - 6.0f, strikeY), strikeBrush.Get(), 1.0f);
        }
    }
    DrawButton(editRect, L"#edit", false, rowAlpha, hovered && m_hoveredKind == HitKind::RowEdit && m_hoveredItemId == item.id);
    DrawButton(deleteRect, L"#delete", false, rowAlpha, hovered && m_hoveredKind == HitKind::RowDelete && m_hoveredItemId == item.id);

    m_hits.push_back({ HitKind::RowToggle, item.id, toggleRect });
    m_hits.push_back({ HitKind::RowEdit, item.id, editRect });
    m_hits.push_back({ HitKind::RowDelete, item.id, deleteRect });
}

void TodoComponent::DrawText(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
    const D2D1_COLOR_F& color, float alpha, DWRITE_TEXT_ALIGNMENT alignment) const {
    if (!m_res || !m_res->d2dContext || !m_res->dwriteFactory || !format || text.empty()) {
        return;
    }

    ComPtr<IDWriteTextLayout> layout;
    const float width = (std::max)(1.0f, rect.right - rect.left);
    const float height = (std::max)(1.0f, rect.bottom - rect.top);
    if (FAILED(m_res->dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, height, &layout))) {
        return;
    }

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(alignment);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(m_res->dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis))) {
        layout->SetTrimming(&trimming, ellipsis.Get());
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    m_res->d2dContext->CreateSolidColorBrush(color, &brush);
    if (!brush) {
        return;
    }
    brush->SetOpacity(alpha);
    m_res->d2dContext->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush.Get());
}

D2D1_COLOR_F TodoComponent::PriorityColor(TodoPriority priority) const {
    switch (priority) {
    case TodoPriority::High: return D2D1::ColorF(0.92f, 0.34f, 0.34f, 1.0f);
    case TodoPriority::Medium: return D2D1::ColorF(0.97f, 0.72f, 0.18f, 1.0f);
    case TodoPriority::Low:
    default:
        return D2D1::ColorF(0.36f, 0.64f, 0.98f, 1.0f);
    }
}

D2D1_COLOR_F TodoComponent::SecondaryFill() const {
    return m_darkMode
        ? D2D1::ColorF(0.15f, 0.15f, 0.18f, 1.0f)
        : D2D1::ColorF(0.94f, 0.94f, 0.97f, 1.0f);
}

D2D1_COLOR_F TodoComponent::StrokeColor() const {
    return m_darkMode
        ? D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f)
        : D2D1::ColorF(0.78f, 0.78f, 0.82f, 1.0f);
}

std::wstring TodoComponent::SummaryText() const {
    if (!m_store) {
        return {};
    }
    if (const TodoItem* top = m_store->GetTopIncomplete()) {
        return top->title;
    }
    return m_store->HasItems() ? L"All done" : L"";
}

void TodoComponent::DrawTodoIcon(const D2D1_RECT_F& rect, const D2D1_COLOR_F& accent, float alpha, bool compact) const {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    if (!brush) {
        return;
    }

    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    const float stroke = compact ? 1.05f : 1.4f;

    if (compact) {
        const float left = rect.left + w * 0.18f;
        const float right = rect.right - w * 0.10f;
        const float top1 = rect.top + h * 0.34f;
        const float top2 = rect.top + h * 0.66f;
        const float box = 1.8f;

        brush->SetOpacity(alpha);
        ctx->DrawRectangle(D2D1::RectF(left, top1 - box, left + box * 2.0f, top1 + box), brush.Get(), stroke);
        brush->SetColor(accent);
        ctx->DrawRectangle(D2D1::RectF(left, top2 - box, left + box * 2.0f, top2 + box), brush.Get(), stroke);

        brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
        ctx->DrawLine(D2D1::Point2F(left + box * 2.7f, top1), D2D1::Point2F(right, top1), brush.Get(), stroke);
        brush->SetColor(accent);
        ctx->DrawLine(D2D1::Point2F(left + box * 2.7f, top2), D2D1::Point2F(right - w * 0.14f, top2), brush.Get(), stroke);
        return;
    }

    const float left = rect.left + w * 0.18f;
    const float right = rect.right - w * 0.08f;
    const float top1 = rect.top + h * 0.22f;
    const float top2 = rect.top + h * 0.50f;
    const float top3 = rect.top + h * 0.78f;
    const float box = 3.0f;

    brush->SetOpacity(alpha);
    ctx->DrawRectangle(D2D1::RectF(left, top1 - box, left + box * 2.0f, top1 + box), brush.Get(), stroke);
    ctx->DrawRectangle(D2D1::RectF(left, top2 - box, left + box * 2.0f, top2 + box), brush.Get(), stroke);
    brush->SetColor(accent);
    ctx->DrawRectangle(D2D1::RectF(left, top3 - box, left + box * 2.0f, top3 + box), brush.Get(), stroke);

    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    ctx->DrawLine(D2D1::Point2F(left + box * 2.8f, top1), D2D1::Point2F(right, top1), brush.Get(), stroke);
    ctx->DrawLine(D2D1::Point2F(left + box * 2.8f, top2), D2D1::Point2F(right - w * 0.10f, top2), brush.Get(), stroke);
    brush->SetColor(accent);
    ctx->DrawLine(D2D1::Point2F(left + box * 2.8f, top3), D2D1::Point2F(right - w * 0.18f, top3), brush.Get(), stroke);
}

void TodoComponent::DrawCloseIcon(const D2D1_RECT_F& rect, float alpha) const {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    if (!brush) return;
    brush->SetOpacity(alpha);
    const float inset = 9.0f;
    ctx->DrawLine(D2D1::Point2F(rect.left + inset, rect.top + inset), D2D1::Point2F(rect.right - inset, rect.bottom - inset), brush.Get(), 1.4f);
    ctx->DrawLine(D2D1::Point2F(rect.right - inset, rect.top + inset), D2D1::Point2F(rect.left + inset, rect.bottom - inset), brush.Get(), 1.4f);
}

void TodoComponent::DrawEditIcon(const D2D1_RECT_F& rect, float alpha) const {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    if (!brush) return;
    brush->SetOpacity(alpha);
    const D2D1_POINT_2F p1 = D2D1::Point2F(rect.left + 13.0f, rect.bottom - 12.0f);
    const D2D1_POINT_2F p2 = D2D1::Point2F(rect.right - 13.0f, rect.top + 12.0f);
    ctx->DrawLine(p1, p2, brush.Get(), 1.5f);
    ctx->DrawLine(D2D1::Point2F(p1.x - 2.0f, p1.y + 4.0f), D2D1::Point2F(p1.x + 5.0f, p1.y + 2.0f), brush.Get(), 1.3f);
    ctx->DrawLine(D2D1::Point2F(p2.x - 2.0f, p2.y - 4.0f), D2D1::Point2F(p2.x + 3.0f, p2.y + 1.0f), brush.Get(), 1.3f);
}

void TodoComponent::DrawDeleteIcon(const D2D1_RECT_F& rect, float alpha) const {
    auto* ctx = m_res->d2dContext;
    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    if (!brush) return;
    brush->SetOpacity(alpha);
    const D2D1_RECT_F canRect = D2D1::RectF(rect.left + 14.0f, rect.top + 14.0f, rect.right - 14.0f, rect.bottom - 10.0f);
    ctx->DrawRectangle(canRect, brush.Get(), 1.3f);
    ctx->DrawLine(D2D1::Point2F(canRect.left - 2.0f, canRect.top), D2D1::Point2F(canRect.right + 2.0f, canRect.top), brush.Get(), 1.3f);
    ctx->DrawLine(D2D1::Point2F(rect.left + 17.0f, canRect.top - 4.0f), D2D1::Point2F(rect.right - 17.0f, canRect.top - 4.0f), brush.Get(), 1.3f);
    ctx->DrawLine(D2D1::Point2F(canRect.left + 5.0f, canRect.top + 4.0f), D2D1::Point2F(canRect.left + 5.0f, canRect.bottom - 3.0f), brush.Get(), 1.1f);
    ctx->DrawLine(D2D1::Point2F((canRect.left + canRect.right) * 0.5f, canRect.top + 4.0f), D2D1::Point2F((canRect.left + canRect.right) * 0.5f, canRect.bottom - 3.0f), brush.Get(), 1.1f);
    ctx->DrawLine(D2D1::Point2F(canRect.right - 5.0f, canRect.top + 4.0f), D2D1::Point2F(canRect.right - 5.0f, canRect.bottom - 3.0f), brush.Get(), 1.1f);
}

void TodoComponent::ResetExpandedEditor() {
    m_editingItemId = 0;
    m_editorTitle.clear();
    m_editorTitleCursor = 0;
    m_editorNote.clear();
    m_editorNoteCursor = 0;
    m_editorPriority = TodoPriority::Medium;
    SetActiveField(ActiveField::None);
}

void TodoComponent::BeginEdit(uint64_t itemId) {
    if (!m_store) {
        return;
    }
    const TodoItem* item = m_store->FindItem(itemId);
    if (!item) {
        return;
    }

    m_editingItemId = itemId;
    m_editorTitle = item->title;
    m_editorTitleCursor = static_cast<int>(m_editorTitle.size());
    m_editorNote = item->note;
    m_editorNoteCursor = static_cast<int>(m_editorNote.size());
    m_editorPriority = item->priority;
    SetActiveField(ActiveField::EditorTitle);
}

void TodoComponent::SaveEditor() {
    if (!m_store) {
        return;
    }

    const std::wstring title = Trimmed(m_editorTitle);
    if (title.empty()) {
        return;
    }

    if (m_editingItemId == 0) {
        m_store->AddItem(title, Trimmed(m_editorNote), m_editorPriority);
    } else {
        m_store->UpdateItem(m_editingItemId, title, Trimmed(m_editorNote), m_editorPriority);
    }
    ResetExpandedEditor();
}

void TodoComponent::SubmitCompactInput() {
    if (m_store) {
        const std::wstring title = Trimmed(m_compactInputText);
        if (!title.empty()) {
            m_store->AddItem(title, L"", TodoPriority::Medium);
        }
    }
    CancelCompactInput();
}

void TodoComponent::CancelCompactInput() {
    m_compactInputText.clear();
    m_compactCursor = 0;
    SetActiveField(ActiveField::None);
    if (m_onRequestCloseInput) {
        m_onRequestCloseInput();
    }
}

void TodoComponent::CancelExpanded() {
    ResetExpandedEditor();
}

void TodoComponent::SetActiveField(ActiveField field) {
    m_activeField = field;
    if (field == ActiveField::CompactInput && m_compactCursor > static_cast<int>(m_compactInputText.size())) {
        m_compactCursor = static_cast<int>(m_compactInputText.size());
    }
    if (field == ActiveField::EditorTitle && m_editorTitleCursor > static_cast<int>(m_editorTitle.size())) {
        m_editorTitleCursor = static_cast<int>(m_editorTitle.size());
    }
    if (field == ActiveField::EditorNote && m_editorNoteCursor > static_cast<int>(m_editorNote.size())) {
        m_editorNoteCursor = static_cast<int>(m_editorNote.size());
    }
}

std::wstring* TodoComponent::ActiveBuffer() {
    switch (m_activeField) {
    case ActiveField::CompactInput: return &m_compactInputText;
    case ActiveField::EditorTitle: return &m_editorTitle;
    case ActiveField::EditorNote: return &m_editorNote;
    case ActiveField::None:
    default:
        return nullptr;
    }
}

int* TodoComponent::ActiveCursor() {
    switch (m_activeField) {
    case ActiveField::CompactInput: return &m_compactCursor;
    case ActiveField::EditorTitle: return &m_editorTitleCursor;
    case ActiveField::EditorNote: return &m_editorNoteCursor;
    case ActiveField::None:
    default:
        return nullptr;
    }
}

std::wstring TodoComponent::Trimmed(const std::wstring& text) const {
    const size_t start = text.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        return {};
    }
    const size_t end = text.find_last_not_of(L" \t\r\n");
    return text.substr(start, end - start + 1);
}

void TodoComponent::MoveCursorLeft() {
    if (int* cursor = ActiveCursor(); cursor && *cursor > 0) {
        --(*cursor);
    }
}

void TodoComponent::MoveCursorRight() {
    if (std::wstring* buffer = ActiveBuffer()) {
        if (int* cursor = ActiveCursor(); cursor && *cursor < static_cast<int>(buffer->size())) {
            ++(*cursor);
        }
    }
}

void TodoComponent::MoveCursorHome() {
    if (int* cursor = ActiveCursor()) {
        *cursor = 0;
    }
}

void TodoComponent::MoveCursorEnd() {
    if (std::wstring* buffer = ActiveBuffer()) {
        if (int* cursor = ActiveCursor()) {
            *cursor = static_cast<int>(buffer->size());
        }
    }
}

void TodoComponent::DeleteBackward() {
    std::wstring* buffer = ActiveBuffer();
    int* cursor = ActiveCursor();
    if (!buffer || !cursor || *cursor <= 0) {
        return;
    }
    buffer->erase(static_cast<size_t>(*cursor - 1), 1);
    --(*cursor);
}

void TodoComponent::DeleteForward() {
    std::wstring* buffer = ActiveBuffer();
    int* cursor = ActiveCursor();
    if (!buffer || !cursor || *cursor >= static_cast<int>(buffer->size())) {
        return;
    }
    buffer->erase(static_cast<size_t>(*cursor), 1);
}

void TodoComponent::InsertText(const std::wstring& text) {
    std::wstring* buffer = ActiveBuffer();
    int* cursor = ActiveCursor();
    if (!buffer || !cursor || text.empty()) {
        return;
    }
    buffer->insert(static_cast<size_t>(*cursor), text);
    *cursor += static_cast<int>(text.size());
}

TodoComponent::HitTarget TodoComponent::HitTestExpanded(float x, float y) const {
    for (const auto& hit : m_hits) {
        if (Contains(hit.rect, x, y)) {
            return hit;
        }
    }
    return {};
}

bool TodoComponent::Contains(const D2D1_RECT_F& rect, float x, float y) const {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}
