#pragma once

#include "IIslandComponent.h"
#include "IslandState.h"
#include "TodoStore.h"
#include <functional>
#include <vector>

class TodoComponent : public IIslandComponent {
public:
    TodoComponent();

    void OnAttach(SharedResources* res) override;
    void Update(float deltaTime) override;
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override;
    bool NeedsRender() const override;
    bool OnMouseWheel(float x, float y, int delta) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseClick(float x, float y) override;
    bool OnChar(wchar_t ch) override;
    bool OnKeyDown(WPARAM key) override;
    bool OnImeComposition(HWND hwnd, LPARAM lParam) override;
    bool OnImeSetContext(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result) override;
    D2D1_RECT_F GetImeAnchorRect() const override;

    void SetStore(TodoStore* store) { m_store = store; }
    void SetDisplayMode(IslandDisplayMode mode) { m_mode = mode; }
    IslandDisplayMode GetDisplayMode() const { return m_mode; }
    void SetDarkMode(bool darkMode) { m_darkMode = darkMode; }
    void BeginCompactInputFocus();
    bool BeginIdleOpenAnimation();
    bool IsLaunchAnimating() const { return m_launchAnimating; }
    float LaunchProgress() const;

    void DrawIdleBadge(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs);
    void DrawIdleLaunchOverlay(const D2D1_RECT_F& contentRect, const D2D1_RECT_F& badgeRect, float contentAlpha, ULONGLONG currentTimeMs);
    bool OnIdleBadgeMouseMove(float x, float y);
    bool IdleBadgeContains(float x, float y) const;

    void SetOnRequestCloseInput(std::function<void()> callback) { m_onRequestCloseInput = std::move(callback); }
    void SetOnRequestCloseExpanded(std::function<void()> callback) { m_onRequestCloseExpanded = std::move(callback); }
    void SetOnLaunchComplete(std::function<void()> callback) { m_onLaunchComplete = std::move(callback); }

private:
    enum class ActiveField {
        None,
        CompactInput,
        EditorTitle,
        EditorNote
    };

    enum class HitKind {
        None,
        HeaderClose,
        TitleField,
        NoteField,
        PriorityHigh,
        PriorityMedium,
        PriorityLow,
        SaveButton,
        CancelButton,
        RowToggle,
        RowEdit,
        RowDelete,
        CompactBody
    };

    struct HitTarget {
        HitKind kind = HitKind::None;
        uint64_t itemId = 0;
        D2D1_RECT_F rect = D2D1::RectF(0, 0, 0, 0);
    };

    void DrawIdleBadgeContent(const D2D1_RECT_F& rect, float alpha);
    void DrawIdleLaunchAnimation(const D2D1_RECT_F& contentRect, const D2D1_RECT_F& badgeRect, float alpha);
    void DrawCompactInput(const D2D1_RECT_F& rect, float alpha);
    void DrawCompactList(const D2D1_RECT_F& rect, float alpha);
    void DrawExpanded(const D2D1_RECT_F& rect, float alpha);
    void DrawField(const D2D1_RECT_F& rect, const std::wstring& value, const std::wstring& placeholder, bool focused, float alpha);
    void DrawButton(const D2D1_RECT_F& rect, const std::wstring& text, bool primary, float alpha, bool hovered = false);
    void DrawPriorityPill(const D2D1_RECT_F& rect, TodoPriority priority, bool selected, float alpha, bool hovered = false);
    void DrawRow(const D2D1_RECT_F& rowRect, const TodoItem& item, float alpha, bool hovered = false);
    void DrawTodoIcon(const D2D1_RECT_F& rect, const D2D1_COLOR_F& accent, float alpha, bool compact) const;
    void DrawCloseIcon(const D2D1_RECT_F& rect, float alpha) const;
    void DrawEditIcon(const D2D1_RECT_F& rect, float alpha) const;
    void DrawDeleteIcon(const D2D1_RECT_F& rect, float alpha) const;
    void DrawText(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        const D2D1_COLOR_F& color, float alpha, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING) const;
    D2D1_COLOR_F PriorityColor(TodoPriority priority) const;
    D2D1_COLOR_F SecondaryFill() const;
    D2D1_COLOR_F StrokeColor() const;
    std::wstring SummaryText() const;

    void ResetExpandedEditor();
    void BeginEdit(uint64_t itemId);
    void SaveEditor();
    void SubmitCompactInput();
    void CancelCompactInput();
    void CancelExpanded();
    void SetActiveField(ActiveField field);
    std::wstring* ActiveBuffer();
    int* ActiveCursor();
    std::wstring Trimmed(const std::wstring& text) const;
    void MoveCursorLeft();
    void MoveCursorRight();
    void MoveCursorHome();
    void MoveCursorEnd();
    void DeleteBackward();
    void DeleteForward();
    void InsertText(const std::wstring& text);
    HitTarget HitTestExpanded(float x, float y) const;
    bool Contains(const D2D1_RECT_F& rect, float x, float y) const;

    SharedResources* m_res = nullptr;
    TodoStore* m_store = nullptr;
    IslandDisplayMode m_mode = IslandDisplayMode::Idle;
    bool m_darkMode = true;
    bool m_idleBadgeHovered = false;
    ActiveField m_activeField = ActiveField::None;
    HitKind m_hoveredKind = HitKind::None;
    uint64_t m_hoveredItemId = 0;
    bool m_launchAnimating = false;
    float m_launchElapsed = 0.0f;

    D2D1_RECT_F m_idleBadgeRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_lastRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_compactInputRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_titleFieldRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_noteFieldRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_listViewportRect = D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F m_closeRect = D2D1::RectF(0, 0, 0, 0);
    std::vector<HitTarget> m_hits;
    float m_listScroll = 0.0f;
    float m_maxScroll = 0.0f;

    std::wstring m_compactInputText;
    int m_compactCursor = 0;
    std::wstring m_editorTitle;
    int m_editorTitleCursor = 0;
    std::wstring m_editorNote;
    int m_editorNoteCursor = 0;
    TodoPriority m_editorPriority = TodoPriority::Medium;
    uint64_t m_editingItemId = 0;

    std::function<void()> m_onRequestCloseInput;
    std::function<void()> m_onRequestCloseExpanded;
    std::function<void()> m_onLaunchComplete;
};
