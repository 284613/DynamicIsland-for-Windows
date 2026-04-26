#include "DynamicIsland.h"

#include <cmath>
#include <sstream>

void AppendInputDebugLog(const std::wstring& message);

namespace {
const wchar_t* SecondaryKindToString(SecondaryContentKind kind) {
    switch (kind) {
    case SecondaryContentKind::None: return L"None";
    case SecondaryContentKind::Volume: return L"Volume";
    case SecondaryContentKind::FileCircle: return L"FileCircle";
    case SecondaryContentKind::FileSwirlDrop: return L"FileSwirlDrop";
    case SecondaryContentKind::FileMini: return L"FileMini";
    case SecondaryContentKind::FileExpanded: return L"FileExpanded";
    case SecondaryContentKind::FileDropTarget: return L"FileDropTarget";
    default: return L"Unknown";
    }
}

const wchar_t* FileHitKindToString(FilePanelComponent::HitResult::Kind kind) {
    switch (kind) {
    case FilePanelComponent::HitResult::Kind::None: return L"None";
    case FilePanelComponent::HitResult::Kind::MiniBody: return L"MiniBody";
    case FilePanelComponent::HitResult::Kind::PreviewPane: return L"PreviewPane";
    case FilePanelComponent::HitResult::Kind::CollapseButton: return L"CollapseButton";
    case FilePanelComponent::HitResult::Kind::ExpandedBackground: return L"ExpandedBackground";
    case FilePanelComponent::HitResult::Kind::FileItem: return L"FileItem";
    default: return L"Unknown";
    }
}
}

void DynamicIsland::ResetFileSecondaryInteraction() {
    m_fileHoveredIndex = -1;
    m_filePressedIndex = -1;
    m_fileLastClickIndex = -1;
    m_fileLastClickTime = 0;
    m_fileDragStarted = false;
    m_filePressPoint = {};
}

void DynamicIsland::ShowFileStashLimitAlert() {
    AlertInfo info{};
    info.type = Constants::Alert::TYPE_FILE;
    info.name = L"文件暂存已满";
    info.deviceType = L"最多暂存 5 个文件";
    info.priority = PRIORITY_P3_BACKGROUND;
    m_alertQueue.push(info);
    ProcessNextAlert();
}

void DynamicIsland::RemoveFileStashIndex(int index) {
    if (index < 0 || index >= (int)m_fileStash.Count()) {
        return;
    }

    m_fileStash.RemoveIndex((size_t)index);
    if (!m_fileStash.HasItems()) {
        m_fileSecondaryExpanded = false;
        m_fileSelectedIndex = -1;
        ResetFileSecondaryInteraction();
    } else if (m_fileSelectedIndex >= (int)m_fileStash.Count()) {
        m_fileSelectedIndex = (int)m_fileStash.Count() - 1;
    }
}

bool DynamicIsland::HandleFileSecondaryMouseDown(POINT pt) {
    const SecondaryContentKind secondary = DetermineSecondaryContent();
    const D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
#ifdef _DEBUG
    {
        std::wostringstream oss;
        oss << L"MouseDown secondary=" << SecondaryKindToString(secondary)
            << L" pt=(" << pt.x << L"," << pt.y << L")"
            << L" rect=(" << secondaryRect.left << L"," << secondaryRect.top << L"," << secondaryRect.right << L"," << secondaryRect.bottom << L")"
            << L" count=" << m_fileStash.Count()
            << L" expanded=" << (m_fileSecondaryExpanded ? 1 : 0);
        AppendInputDebugLog(oss.str());
    }
#endif
    if (secondary != SecondaryContentKind::FileCircle &&
        secondary != SecondaryContentKind::FileSwirlDrop &&
        secondary != SecondaryContentKind::FileMini &&
        secondary != SecondaryContentKind::FileExpanded &&
        secondary != SecondaryContentKind::FileDropTarget) {
        AppendInputDebugLog(L"MouseDown ignored: secondary content not file-related");
        return false;
    }

    if ((float)pt.x < secondaryRect.left || (float)pt.x > secondaryRect.right ||
        (float)pt.y < secondaryRect.top || (float)pt.y > secondaryRect.bottom) {
        AppendInputDebugLog(L"MouseDown ignored: point outside secondary rect");
        return false;
    }

    const auto hit = m_renderer.HitTestFileSecondary((float)pt.x, (float)pt.y);
#ifdef _DEBUG
    {
        std::wostringstream oss;
        oss << L"MouseDown hit=" << FileHitKindToString(hit.kind) << L" index=" << hit.index;
        AppendInputDebugLog(oss.str());
    }
#endif
    if (secondary == SecondaryContentKind::FileMini || secondary == SecondaryContentKind::FileDropTarget) {
        m_fileSecondaryExpanded = m_fileStash.HasItems();
        StartAnimation();
        AppendInputDebugLog(m_fileSecondaryExpanded ? L"MouseDown action: expand file secondary"
                                                    : L"MouseDown action: cannot expand because stash empty");
        Invalidate(Dirty_FileDrop | Dirty_Region);
        return true;
    }

    if (hit.kind == FilePanelComponent::HitResult::Kind::CollapseButton ||
        hit.kind == FilePanelComponent::HitResult::Kind::ExpandedBackground) {
        m_fileSecondaryExpanded = false;
        ResetFileSecondaryInteraction();
        StartAnimation();
        AppendInputDebugLog(L"MouseDown action: collapse file secondary");
        Invalidate(Dirty_FileDrop | Dirty_Region);
        return true;
    }

    if (hit.kind == FilePanelComponent::HitResult::Kind::PreviewPane) {
        AppendInputDebugLog(L"MouseDown action: consume preview pane");
        return true;
    }

    if (hit.kind == FilePanelComponent::HitResult::Kind::FileItem) {
        m_fileSelectedIndex = hit.index;
        m_filePressedIndex = hit.index;
        m_filePressPoint = pt;
        m_fileDragStarted = false;
        Invalidate(Dirty_FileDrop);
        AppendInputDebugLog(L"MouseDown action: press file item");
        return true;
    }

    AppendInputDebugLog(L"MouseDown action: consumed with no state change");
    return true;
}

bool DynamicIsland::HandleFileSecondaryMouseMove(HWND hwnd, POINT pt, WPARAM keyState) {
    const SecondaryContentKind secondary = DetermineSecondaryContent();
    const bool secondaryVisible = secondary == SecondaryContentKind::FileCircle ||
        secondary == SecondaryContentKind::FileSwirlDrop ||
        secondary == SecondaryContentKind::FileMini ||
        secondary == SecondaryContentKind::FileExpanded ||
        secondary == SecondaryContentKind::FileDropTarget;
    const D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
    const bool insideSecondary = secondaryVisible &&
        (float)pt.x >= secondaryRect.left && (float)pt.x <= secondaryRect.right &&
        (float)pt.y >= secondaryRect.top && (float)pt.y <= secondaryRect.bottom;

    if (insideSecondary) {
        const auto hit = m_renderer.HitTestFileSecondary((float)pt.x, (float)pt.y);
        const int newHovered = hit.kind == FilePanelComponent::HitResult::Kind::FileItem ? hit.index : -1;
        if (newHovered != m_fileHoveredIndex) {
#ifdef _DEBUG
            std::wostringstream oss;
            oss << L"MouseMove hover change: secondary=" << SecondaryKindToString(secondary)
                << L" hit=" << FileHitKindToString(hit.kind) << L" index=" << hit.index;
            AppendInputDebugLog(oss.str());
#endif
            m_fileHoveredIndex = newHovered;
            Invalidate(Dirty_Hover | Dirty_FileDrop);
        }
    } else if (m_fileHoveredIndex != -1) {
        m_fileHoveredIndex = -1;
        Invalidate(Dirty_Hover | Dirty_FileDrop);
    }

    if (m_filePressedIndex != -1 && (keyState & MK_LBUTTON)) {
        const int dx = pt.x - m_filePressPoint.x;
        const int dy = pt.y - m_filePressPoint.y;
        if (std::abs(dx) >= GetSystemMetrics(SM_CXDRAG) || std::abs(dy) >= GetSystemMetrics(SM_CYDRAG)) {
            bool moved = false;
            m_fileDragStarted = true;
            m_fileSelfDropDetected = false;
            AppendInputDebugLog(L"MouseMove action: begin file drag");
            m_fileStash.BeginMoveDrag(hwnd, (size_t)m_filePressedIndex, moved);
            if (moved) {
                if (m_fileSelfDropDetected) {
                    AppendInputDebugLog(L"MouseMove action: ignore self-drop back into stash");
                } else {
                    AppendInputDebugLog(L"MouseMove action: drag completed with move effect");
                    RemoveFileStashIndex(m_filePressedIndex);
                }
            }
            m_fileSelfDropDetected = false;
            m_filePressedIndex = -1;
            Invalidate(Dirty_FileDrop | Dirty_Region);
            return true;
        }
    }

    return insideSecondary;
}

bool DynamicIsland::HandleFileSecondaryMouseUp(POINT pt) {
    const SecondaryContentKind secondary = DetermineSecondaryContent();
    const bool secondaryVisible = secondary == SecondaryContentKind::FileCircle ||
        secondary == SecondaryContentKind::FileSwirlDrop ||
        secondary == SecondaryContentKind::FileMini ||
        secondary == SecondaryContentKind::FileExpanded ||
        secondary == SecondaryContentKind::FileDropTarget;
    const D2D1_RECT_F secondaryRect = GetSecondaryRectLogical();
    const bool isOverSecondary = secondaryVisible &&
        (float)pt.x >= secondaryRect.left && (float)pt.x <= secondaryRect.right &&
        (float)pt.y >= secondaryRect.top && (float)pt.y <= secondaryRect.bottom;
    const auto hit = isOverSecondary
        ? m_renderer.HitTestFileSecondary((float)pt.x, (float)pt.y)
        : FilePanelComponent::HitResult{};
#ifdef _DEBUG
    {
        std::wostringstream oss;
        oss << L"MouseUp secondary=" << SecondaryKindToString(secondary)
            << L" pt=(" << pt.x << L"," << pt.y << L")"
            << L" inside=" << (isOverSecondary ? 1 : 0)
            << L" hit=" << FileHitKindToString(hit.kind) << L" index=" << hit.index
            << L" pressedIndex=" << m_filePressedIndex
            << L" dragStarted=" << (m_fileDragStarted ? 1 : 0);
        AppendInputDebugLog(oss.str());
    }
#endif

    if (m_filePressedIndex != -1 && !m_fileDragStarted &&
        hit.kind == FilePanelComponent::HitResult::Kind::FileItem &&
        hit.index == m_filePressedIndex) {
        m_fileSelectedIndex = hit.index;
        const ULONGLONG now = GetTickCount64();
        const bool isDoubleClick = m_fileLastClickIndex == hit.index &&
            (now - m_fileLastClickTime <= (ULONGLONG)GetDoubleClickTime());
        if (isDoubleClick) {
            m_fileStash.OpenIndex((size_t)hit.index);
            m_fileLastClickIndex = -1;
            m_fileLastClickTime = 0;
            AppendInputDebugLog(L"MouseUp action: open file item");
        } else {
            m_fileStash.PreviewIndex((size_t)hit.index);
            m_fileLastClickIndex = hit.index;
            m_fileLastClickTime = now;
            AppendInputDebugLog(L"MouseUp action: preview file item");
        }
        Invalidate(Dirty_FileDrop);
        m_filePressedIndex = -1;
        return true;
    }

    m_filePressedIndex = -1;
    m_fileDragStarted = false;
    AppendInputDebugLog(isOverSecondary ? L"MouseUp action: consume secondary click"
                                        : L"MouseUp ignored outside secondary");
    return isOverSecondary;
}
