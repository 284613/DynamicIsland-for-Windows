#include "components/FilePanelComponent.h"
#include "Constants.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <wincodec.h>
#include <cmath>
#include <cstdint>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

float SmoothStep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    float t = Clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

D2D1_RECT_F OffsetRect(const D2D1_RECT_F& rect, float dx, float dy) {
    return D2D1::RectF(rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy);
}

bool RectContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

bool FileItemsEquivalent(const FileStashItem& lhs, const FileStashItem& rhs) {
    return lhs.stagedPath == rhs.stagedPath &&
        lhs.displayName == rhs.displayName &&
        lhs.extension == rhs.extension &&
        lhs.sizeBytes == rhs.sizeBytes;
}
} // namespace

FilePanelComponent::FilePanelComponent() {}
FilePanelComponent::~FilePanelComponent() {}

bool FilePanelComponent::Initialize() {
    return true;
}

void FilePanelComponent::OnAttach(SharedResources* res) {
    m_res = res;
    ClearTextLayoutCache();
}

bool FilePanelComponent::HasSameStoredFiles(const std::vector<FileStashItem>& files) const {
    if (m_storedFiles.size() != files.size()) {
        return false;
    }

    for (size_t i = 0; i < files.size(); ++i) {
        if (!FileItemsEquivalent(m_storedFiles[i], files[i])) {
            return false;
        }
    }
    return true;
}

void FilePanelComponent::ClearTextLayoutCache() {
    m_textLayoutCache.clear();
}

void FilePanelComponent::SetStoredFiles(const std::vector<FileStashItem>& files) {
    if (HasSameStoredFiles(files)) {
        return;
    }

    m_storedFiles = files;
    ClearTextLayoutCache();
    if (m_selectedFileIndex >= (int)m_storedFiles.size()) {
        m_selectedFileIndex = m_storedFiles.empty() ? -1 : 0;
    }

    if (m_res) {
        for (const auto& file : m_storedFiles) {
            GetFileIcon(file.stagedPath);
        }
    }
}

void FilePanelComponent::SetInteractionState(int selectedIndex, int hoveredIndex) {
    m_selectedFileIndex = selectedIndex;
    m_hoveredFileIndex = hoveredIndex;
}

void FilePanelComponent::Update(float deltaTime) {
    const float dt = (std::max)(0.001f, (std::min)(0.05f, deltaTime > 0.0f ? deltaTime : 0.016f));
    if (m_viewMode == ViewMode::SwirlDrop || m_viewMode == ViewMode::CircleDropTarget) {
        m_phase = std::fmod(m_phase + dt * 4.2f, 6.2831853f);
    }
}

bool FilePanelComponent::ContainsPoint(float x, float y) const {
    return x >= m_lastRect.left && x <= m_lastRect.right && y >= m_lastRect.top && y <= m_lastRect.bottom;
}

float FilePanelComponent::GetExpansionProgress(const D2D1_RECT_F& rect) const {
    float height = rect.bottom - rect.top;
    float minHeight = Constants::Size::SECONDARY_HEIGHT;
    float maxHeight = Constants::Size::FILE_SECONDARY_EXPANDED_HEIGHT;
    if (maxHeight <= minHeight) {
        return 1.0f;
    }
    return Clamp01((height - minHeight) / (maxHeight - minHeight));
}

D2D1_RECT_F FilePanelComponent::GetPreviewRect(const D2D1_RECT_F& rect) const {
    return D2D1::RectF(rect.left + 8.0f, rect.top + 8.0f, rect.right - 8.0f, rect.top + PREVIEW_HEIGHT);
}

D2D1_RECT_F FilePanelComponent::GetCollapseButtonRect(const D2D1_RECT_F& rect) const {
    D2D1_RECT_F previewRect = GetPreviewRect(rect);
    float buttonWidth = 48.0f;
    float buttonHeight = 20.0f;
    float rightInset = 10.0f;
    float topInset = 8.0f;
    return D2D1::RectF(
        previewRect.right - buttonWidth - rightInset,
        previewRect.top + topInset,
        previewRect.right - rightInset,
        previewRect.top + topInset + buttonHeight);
}

D2D1_RECT_F FilePanelComponent::GetRowRect(const D2D1_RECT_F& rect, int index) const {
    float listTop = GetPreviewRect(rect).bottom + 8.0f;
    float rowTop = listTop + (float)index * ITEM_HEIGHT;
    return D2D1::RectF(rect.left + 8.0f, rowTop, rect.right - 8.0f, rowTop + ITEM_HEIGHT - 1.0f);
}

FilePanelComponent::HitResult FilePanelComponent::HitTest(float x, float y) const {
    HitResult result;
    if (!ContainsPoint(x, y)) return result;

    if (m_viewMode == ViewMode::Circle || m_viewMode == ViewMode::CircleDropTarget) {
        result.kind = m_storedFiles.empty() ? HitResult::Kind::MiniBody : HitResult::Kind::FileItem;
        result.index = m_storedFiles.empty() ? -1 : 0;
        return result;
    }

    if (m_viewMode == ViewMode::SwirlDrop || m_viewMode == ViewMode::Mini || m_viewMode == ViewMode::DropTarget) {
        result.kind = HitResult::Kind::MiniBody;
        return result;
    }

    float expansionProgress = GetExpansionProgress(m_lastRect);
    if (expansionProgress < 0.30f) {
        result.kind = HitResult::Kind::PreviewPane;
        return result;
    }

    D2D1_RECT_F collapseRect = GetCollapseButtonRect(m_lastRect);
    if (RectContainsPoint(collapseRect, x, y)) {
        result.kind = HitResult::Kind::CollapseButton;
        return result;
    }

    D2D1_RECT_F previewRect = GetPreviewRect(m_lastRect);
    if (RectContainsPoint(previewRect, x, y)) {
        result.kind = HitResult::Kind::PreviewPane;
        return result;
    }

    int maxVisibleItems = (int)(std::min)(m_storedFiles.size(), FileStashStore::GetMaxItems());
    for (int index = 0; index < maxVisibleItems; ++index) {
        D2D1_RECT_F rowRect = GetRowRect(m_lastRect, index);
        if (RectContainsPoint(rowRect, x, y)) {
            result.kind = HitResult::Kind::FileItem;
            result.index = index;
            return result;
        }
    }

    result.kind = HitResult::Kind::ExpandedBackground;
    return result;
}

void FilePanelComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_res->d2dContext || m_viewMode == ViewMode::Hidden || contentAlpha <= 0.01f) return;
    m_lastRect = rect;

    if (m_viewMode == ViewMode::Circle || m_viewMode == ViewMode::CircleDropTarget) {
        RenderCircleView(rect, contentAlpha, m_viewMode == ViewMode::CircleDropTarget);
        return;
    }

    if (m_viewMode == ViewMode::SwirlDrop) {
        RenderSwirlDrop(rect, contentAlpha);
        return;
    }

    if (m_viewMode == ViewMode::DropTarget) {
        RenderDragHint(rect, contentAlpha);
        return;
    }

    float expansionProgress = GetExpansionProgress(rect);
    float compactAlpha = contentAlpha * (1.0f - SmoothStep(0.16f, 0.72f, expansionProgress));
    float expandedAlpha = contentAlpha * SmoothStep(0.20f, 0.96f, expansionProgress);

    m_res->d2dContext->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (compactAlpha > 0.01f) {
        float compactOffsetY = -4.0f * SmoothStep(0.0f, 1.0f, expansionProgress);
        RenderCompactView(OffsetRect(rect, 0.0f, compactOffsetY), compactAlpha);
    }

    if (expandedAlpha > 0.01f) {
        float expandedOffsetY = Lerp(10.0f, 0.0f, SmoothStep(0.0f, 1.0f, expansionProgress));
        RenderExpandedView(OffsetRect(rect, 0.0f, expandedOffsetY), expandedAlpha, expansionProgress);
    }

    m_res->d2dContext->PopAxisAlignedClip();
}

void FilePanelComponent::RenderCircleView(const D2D1_RECT_F& rect, float contentAlpha, bool dropTarget) {
    auto* ctx = m_res->d2dContext;
    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float size = (std::min)(width, height);
    const D2D1_POINT_2F center = D2D1::Point2F(rect.left + width * 0.5f, rect.top + height * 0.5f);
    const float radius = size * 0.5f;

    m_res->blackBrush->SetOpacity(0.72f * contentAlpha);
    ctx->FillEllipse(D2D1::Ellipse(center, radius, radius), m_res->blackBrush);
    m_res->fileBrush->SetOpacity((dropTarget ? 0.95f : 0.70f) * contentAlpha);
    ctx->DrawEllipse(D2D1::Ellipse(center, radius - 1.5f, radius - 1.5f), m_res->fileBrush, dropTarget ? 2.2f : 1.4f);

    if (dropTarget) {
        for (int i = 0; i < 3; ++i) {
            const float r = radius - 6.0f - static_cast<float>(i) * 5.5f;
            const float a = m_phase + static_cast<float>(i) * 2.1f;
            const D2D1_POINT_2F p1 = D2D1::Point2F(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
            const D2D1_POINT_2F p2 = D2D1::Point2F(center.x + std::cos(a + 1.15f) * (r - 3.0f), center.y + std::sin(a + 1.15f) * (r - 3.0f));
            m_res->themeBrush->SetOpacity((0.42f - i * 0.08f) * contentAlpha);
            ctx->DrawLine(p1, p2, m_res->themeBrush, 1.4f);
        }
    }

    const float iconSize = size * 0.48f;
    const float iconLeft = center.x - iconSize * 0.5f;
    const float iconTop = center.y - iconSize * 0.5f;
    if (!m_storedFiles.empty()) {
        auto icon = GetFileIcon(m_storedFiles[0].stagedPath);
        if (icon) {
            ctx->DrawBitmap(icon.Get(), D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }

    m_res->fileBrush->SetOpacity(contentAlpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), 6.0f, 6.0f), m_res->fileBrush);
}

void FilePanelComponent::RenderSwirlDrop(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const D2D1_POINT_2F center = D2D1::Point2F(rect.left + width * 0.5f, rect.top + height * 0.52f);
    const float maxRadius = (std::min)(width, height) * 0.36f;

    m_res->themeBrush->SetOpacity(0.12f * contentAlpha);
    ctx->FillEllipse(D2D1::Ellipse(center, maxRadius * 1.05f, maxRadius * 0.72f), m_res->themeBrush);

    if (m_res->d2dFactory) {
        for (int arm = 0; arm < 3; ++arm) {
            ComPtr<ID2D1PathGeometry> geometry;
            if (FAILED(m_res->d2dFactory->CreatePathGeometry(&geometry)) || !geometry) {
                continue;
            }
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geometry->Open(&sink)) || !sink) {
                continue;
            }
            const float startAngle = m_phase + static_cast<float>(arm) * 2.0943951f;
            for (int i = 0; i <= 24; ++i) {
                const float t = static_cast<float>(i) / 24.0f;
                const float angle = startAngle + t * 4.8f;
                const float radius = maxRadius * (1.0f - t * 0.82f);
                const D2D1_POINT_2F p = D2D1::Point2F(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius * 0.72f);
                if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
                else sink->AddLine(p);
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close())) {
                m_res->themeBrush->SetOpacity((0.58f - arm * 0.08f) * contentAlpha);
                ctx->DrawGeometry(geometry.Get(), m_res->themeBrush, 2.0f);
            }
        }
    }

    const float iconSize = 24.0f + 4.0f * std::sinf(m_phase * 1.7f);
    const float iconLeft = center.x - iconSize * 0.5f;
    const float iconTop = center.y - iconSize * 0.5f;
    m_res->fileBrush->SetOpacity(0.92f * contentAlpha);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), 7.0f, 7.0f), m_res->fileBrush);

    auto titleLayout = CreateTextLayout(L"Drop file", m_res->subFormat, width - 36.0f);
    if (titleLayout) {
        DWRITE_TEXT_METRICS metrics{};
        titleLayout->GetMetrics(&metrics);
        m_res->whiteBrush->SetOpacity(0.78f * contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(center.x - metrics.width * 0.5f, rect.bottom - metrics.height - 8.0f), titleLayout.Get(), m_res->whiteBrush);
    }
}

void FilePanelComponent::RenderDragHint(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;
    bool isFull = m_storedFiles.size() >= FileStashStore::GetMaxItems();

    m_res->themeBrush->SetOpacity((isFull ? 0.28f : 0.18f) * contentAlpha);
    D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(rect, height / 2.0f, height / 2.0f);
    ctx->DrawRoundedRectangle(&bgRect, m_res->themeBrush, isFull ? 1.8f : 1.4f);

    float iconSize = 22.0f;
    float iconX = left + 18.0f;
    float iconY = top + 10.0f;
    m_res->themeBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(isFull ? L"\uE783" : L"\uE8E5", 1, m_res->iconFormat,
        D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize), m_res->themeBrush);

    std::wstring title = isFull ? L"暂存已满" : L"拖入以剪切暂存";
    std::wstring subtitle = isFull
        ? (L"最多 " + std::to_wstring(FileStashStore::GetMaxItems()) + L" 个文件，请先移出再拖入")
        : L"松开后加入文件副岛";
    float textLeft = iconX + iconSize + 12.0f;
    float textWidth = width - (textLeft - left) - 16.0f;

    auto titleLayout = CreateTextLayout(title, m_res->titleFormat, textWidth);
    if (titleLayout) {
        DWRITE_TEXT_METRICS metrics{};
        titleLayout->GetMetrics(&metrics);
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, top + 8.0f), titleLayout.Get(), m_res->whiteBrush);
    }

    auto subtitleLayout = CreateTextLayout(subtitle, m_res->subFormat, textWidth);
    if (subtitleLayout) {
        DWRITE_TEXT_METRICS metrics{};
        subtitleLayout->GetMetrics(&metrics);
        m_res->grayBrush->SetOpacity((isFull ? 0.92f : 0.82f) * contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, top + height - metrics.height - 9.0f), subtitleLayout.Get(), m_res->grayBrush);
    }
}

void FilePanelComponent::RenderCompactView(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;
    D2D1_ROUNDED_RECT capsuleRect = D2D1::RoundedRect(rect, height / 2.0f, height / 2.0f);
    m_res->whiteBrush->SetOpacity(0.10f * contentAlpha);
    ctx->DrawRoundedRectangle(&capsuleRect, m_res->whiteBrush, 1.0f);

    int iconCount = (int)(std::min)(m_storedFiles.size(), (size_t)3);
    float stackX = left + 18.0f;
    float stackY = top + 8.0f;
    for (int i = 0; i < iconCount; ++i) {
        float offset = (float)i * 8.0f;
        D2D1_RECT_F iconRect = D2D1::RectF(stackX + offset, stackY - offset * 0.2f, stackX + offset + 20.0f, stackY + 20.0f - offset * 0.2f);
        m_res->fileBrush->SetOpacity((0.85f - i * 0.15f) * contentAlpha);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(iconRect, 4.0f, 4.0f), m_res->fileBrush);
    }

    bool isFull = m_storedFiles.size() >= FileStashStore::GetMaxItems();
    std::wstring countText = isFull
        ? (std::to_wstring(m_storedFiles.size()) + L" / " + std::to_wstring(FileStashStore::GetMaxItems()) + L" 已满")
        : (std::to_wstring(m_storedFiles.size()) + L" 个文件");
    auto countLayout = CreateTextLayout(countText, m_res->titleFormat, width - 86.0f);
    if (countLayout) {
        DWRITE_TEXT_METRICS metrics{};
        countLayout->GetMetrics(&metrics);
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(left + 58.0f, top + (height - metrics.height) / 2.0f), countLayout.Get(), m_res->whiteBrush);
    }
}

void FilePanelComponent::RenderPreviewPane(const D2D1_RECT_F& rect, float contentAlpha, float revealProgress) {
    if (m_storedFiles.empty() || m_selectedFileIndex < 0 || m_selectedFileIndex >= (int)m_storedFiles.size()) return;

    auto* ctx = m_res->d2dContext;
    const auto& item = m_storedFiles[m_selectedFileIndex];
    D2D1_RECT_F previewRect = GetPreviewRect(rect);
    D2D1_RECT_F collapseRect = GetCollapseButtonRect(rect);

    m_res->blackBrush->SetOpacity(0.18f * contentAlpha);
    D2D1_ROUNDED_RECT previewRoundedRect = D2D1::RoundedRect(previewRect, 12.0f, 12.0f);
    ctx->FillRoundedRectangle(&previewRoundedRect, m_res->blackBrush);

    m_res->whiteBrush->SetOpacity((0.08f + 0.06f * revealProgress) * contentAlpha);
    D2D1_ROUNDED_RECT collapseRoundedRect = D2D1::RoundedRect(collapseRect, 8.0f, 8.0f);
    ctx->FillRoundedRectangle(&collapseRoundedRect, m_res->whiteBrush);

    auto collapseLayout = CreateTextLayout(L"收起", m_res->subFormat, collapseRect.right - collapseRect.left);
    if (collapseLayout) {
        DWRITE_TEXT_METRICS metrics{};
        collapseLayout->GetMetrics(&metrics);
        m_res->whiteBrush->SetOpacity(0.78f * contentAlpha);
        ctx->DrawTextLayout(
            D2D1::Point2F(collapseRect.left + ((collapseRect.right - collapseRect.left) - metrics.width) / 2.0f,
                          collapseRect.top + ((collapseRect.bottom - collapseRect.top) - metrics.height) / 2.0f),
            collapseLayout.Get(),
            m_res->whiteBrush);
    }

    float iconLeft = previewRect.left + 12.0f;
    float iconTop = previewRect.top + 13.0f;
    float iconSize = 34.0f;
    auto icon = GetFileIcon(item.stagedPath);
    if (icon) {
        ctx->DrawBitmap(icon.Get(), D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else {
        m_res->fileBrush->SetOpacity(contentAlpha);
        D2D1_ROUNDED_RECT iconRect = D2D1::RoundedRect(D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), 8.0f, 8.0f);
        ctx->FillRoundedRectangle(&iconRect, m_res->fileBrush);
    }

    float textLeft = iconLeft + iconSize + 12.0f;
    float textRight = collapseRect.left - 8.0f;
    auto nameLayout = CreateTextLayout(item.displayName, m_res->titleFormat, (std::max)(40.0f, textRight - textLeft));
    if (nameLayout) {
        nameLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, previewRect.top + 10.0f), nameLayout.Get(), m_res->whiteBrush);
    }

    std::wstring meta = item.extension.empty() ? L"系统文件" : item.extension + L" · " + std::to_wstring((item.sizeBytes + 1023) / 1024) + L" KB";
    auto metaLayout = CreateTextLayout(meta, m_res->subFormat, (std::max)(40.0f, textRight - textLeft));
    if (metaLayout) {
        m_res->grayBrush->SetOpacity(0.82f * contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, previewRect.top + 34.0f), metaLayout.Get(), m_res->grayBrush);
    }
}

void FilePanelComponent::RenderExpandedView(const D2D1_RECT_F& rect, float contentAlpha, float expansionProgress) {
    auto* ctx = m_res->d2dContext;
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(rect, 18.0f, 18.0f);
    m_res->whiteBrush->SetOpacity(0.10f * contentAlpha);
    ctx->DrawRoundedRectangle(&panelRect, m_res->whiteBrush, 1.0f);

    float previewReveal = SmoothStep(0.18f, 0.70f, expansionProgress);
    float listReveal = SmoothStep(0.42f, 1.0f, expansionProgress);

    if (previewReveal > 0.01f) {
        RenderPreviewPane(rect, contentAlpha * previewReveal, previewReveal);
    }

    if (listReveal <= 0.01f) {
        return;
    }

    int maxVisibleItems = (int)(std::min)(m_storedFiles.size(), FileStashStore::GetMaxItems());
    float listOffsetY = Lerp(8.0f, 0.0f, listReveal);
    for (int i = 0; i < maxVisibleItems; ++i) {
        float rowFade = contentAlpha * SmoothStep(0.42f + i * 0.07f, 1.0f, expansionProgress);
        if (rowFade <= 0.01f) {
            continue;
        }

        D2D1_RECT_F rowRect = OffsetRect(GetRowRect(rect, i), 0.0f, listOffsetY);

        if (i == m_selectedFileIndex) {
            m_res->fileBrush->SetOpacity(0.18f * rowFade);
            D2D1_ROUNDED_RECT rowRoundedRect = D2D1::RoundedRect(rowRect, 10.0f, 10.0f);
            ctx->FillRoundedRectangle(&rowRoundedRect, m_res->fileBrush);
        }
        else if (i == m_hoveredFileIndex) {
            m_res->whiteBrush->SetOpacity(0.08f * rowFade);
            D2D1_ROUNDED_RECT rowRoundedRect = D2D1::RoundedRect(rowRect, 10.0f, 10.0f);
            ctx->FillRoundedRectangle(&rowRoundedRect, m_res->whiteBrush);
        }

        auto icon = GetFileIcon(m_storedFiles[i].stagedPath);
        float iconLeft = rowRect.left + 8.0f;
        float iconTop = rowRect.top + ((rowRect.bottom - rowRect.top) - 16.0f) / 2.0f;
        if (icon) {
            ctx->DrawBitmap(icon.Get(), D2D1::RectF(iconLeft, iconTop, iconLeft + 16.0f, iconTop + 16.0f), rowFade, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        else {
            m_res->fileBrush->SetOpacity(rowFade);
            D2D1_ROUNDED_RECT smallIconRect = D2D1::RoundedRect(D2D1::RectF(iconLeft, iconTop, iconLeft + 16.0f, iconTop + 16.0f), 4.0f, 4.0f);
            ctx->FillRoundedRectangle(&smallIconRect, m_res->fileBrush);
        }

        auto nameLayout = CreateTextLayout(m_storedFiles[i].displayName, m_res->subFormat, rowRect.right - iconLeft - 28.0f);
        if (nameLayout) {
            nameLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_res->whiteBrush->SetOpacity(rowFade);
            ctx->DrawTextLayout(D2D1::Point2F(iconLeft + 24.0f, rowRect.top + 2.0f), nameLayout.Get(), m_res->whiteBrush);
        }
    }
}

ComPtr<IDWriteTextLayout> FilePanelComponent::CreateTextLayout(const std::wstring& text, IDWriteTextFormat* format, float maxWidth) {
    if (!m_res || !m_res->dwriteFactory || !format) return nullptr;

    std::wstring formatTag = L"custom";
    if (format == m_res->titleFormat) formatTag = L"title";
    else if (format == m_res->subFormat) formatTag = L"sub";
    else if (format == m_res->iconFormat) formatTag = L"icon";

    std::wstring cacheKey = formatTag + L"|" +
        std::to_wstring((long long)std::llround(maxWidth * 10.0f)) + L"|" + text;

    auto found = m_textLayoutCache.find(cacheKey);
    if (found != m_textLayoutCache.end()) {
        return found->second;
    }

    ComPtr<IDWriteTextLayout> layout;
    if (SUCCEEDED(m_res->dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(), format, maxWidth, 100.0f, &layout))) {
        m_textLayoutCache.emplace(cacheKey, layout);
    }
    return layout;
}

ComPtr<ID2D1Bitmap> FilePanelComponent::GetFileIcon(const std::wstring& path) {
    if (!m_res || !m_res->d2dContext || !m_res->wicFactory) return nullptr;

    std::wstring ext = L"default";
    size_t lastDot = path.find_last_of(L".");
    if (lastDot != std::wstring::npos) {
        ext = path.substr(lastDot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    std::wstring cacheKey = (ext == L".exe" || ext == L".lnk") ? path : ext;
    auto cached = m_iconCache.find(cacheKey);
    if (cached != m_iconCache.end()) {
        return cached->second;
    }

    SHFILEINFOW sfi = { 0 };
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
        if (sfi.hIcon) {
            ComPtr<IWICBitmap> wicBitmap;
            if (SUCCEEDED(m_res->wicFactory->CreateBitmapFromHICON(sfi.hIcon, &wicBitmap))) {
                ComPtr<ID2D1Bitmap> d2dBitmap;
                if (SUCCEEDED(m_res->d2dContext->CreateBitmapFromWicBitmap(wicBitmap.Get(), &d2dBitmap))) {
                    m_iconCache.emplace(cacheKey, d2dBitmap);
                    DestroyIcon(sfi.hIcon);
                    return d2dBitmap;
                }
            }
            DestroyIcon(sfi.hIcon);
        }
    }

    return nullptr;
}
