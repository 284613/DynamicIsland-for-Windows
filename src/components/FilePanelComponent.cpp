#include "components/FilePanelComponent.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <wincodec.h>
#include <cmath>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")

FilePanelComponent::FilePanelComponent() {}
FilePanelComponent::~FilePanelComponent() {}

bool FilePanelComponent::Initialize() {
    return true;
}

void FilePanelComponent::OnAttach(SharedResources* res) {
    m_res = res;
}

void FilePanelComponent::SetStoredFiles(const std::vector<FileStashItem>& files) {
    m_storedFiles = files;
    if (m_selectedFileIndex >= (int)m_storedFiles.size()) {
        m_selectedFileIndex = m_storedFiles.empty() ? -1 : 0;
    }
}

void FilePanelComponent::SetInteractionState(int selectedIndex, int hoveredIndex) {
    m_selectedFileIndex = selectedIndex;
    m_hoveredFileIndex = hoveredIndex;
}

bool FilePanelComponent::ContainsPoint(float x, float y) const {
    return x >= m_lastRect.left && x <= m_lastRect.right && y >= m_lastRect.top && y <= m_lastRect.bottom;
}

FilePanelComponent::HitResult FilePanelComponent::HitTest(float x, float y) const {
    HitResult result;
    if (!ContainsPoint(x, y)) return result;

    if (m_viewMode == ViewMode::Mini || m_viewMode == ViewMode::DropTarget) {
        result.kind = HitResult::Kind::MiniBody;
        return result;
    }

    if (m_viewMode != ViewMode::Expanded) {
        return result;
    }

    float listTop = m_lastRect.top + PREVIEW_HEIGHT + 8.0f;
    if (y < listTop) {
        result.kind = HitResult::Kind::ExpandedBackground;
        return result;
    }

    int index = (int)((y - listTop) / ITEM_HEIGHT);
    if (index >= 0 && index < (int)m_storedFiles.size()) {
        result.kind = HitResult::Kind::FileItem;
        result.index = index;
        return result;
    }

    result.kind = HitResult::Kind::ExpandedBackground;
    return result;
}

void FilePanelComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_res->d2dContext || m_viewMode == ViewMode::Hidden || contentAlpha <= 0.01f) return;
    m_lastRect = rect;
    float height = rect.bottom - rect.top;
    bool treatAsExpanded = (height >= 90.0f);
    bool treatAsMini = (height <= 64.0f);

    switch (m_viewMode) {
    case ViewMode::DropTarget:
        RenderDragHint(rect, contentAlpha);
        break;
    case ViewMode::Mini:
        if (treatAsMini) RenderCompactView(rect, contentAlpha);
        else RenderExpandedView(rect, contentAlpha);
        break;
    case ViewMode::Expanded:
        if (treatAsExpanded) RenderExpandedView(rect, contentAlpha);
        else RenderCompactView(rect, contentAlpha);
        break;
    case ViewMode::Hidden:
    default:
        break;
    }
}

void FilePanelComponent::RenderDragHint(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;

    m_res->whiteBrush->SetOpacity(0.08f * contentAlpha);
    D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(rect, height / 2.0f, height / 2.0f);
    ctx->DrawRoundedRectangle(&bgRect, m_res->whiteBrush, 1.2f);

    float iconSize = 22.0f;
    float iconY = top + (height - iconSize) / 2.0f;
    float iconX = left + 18.0f;
    m_res->themeBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(L"\uE8E5", 1, m_res->iconFormat,
        D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize), m_res->themeBrush);

    std::wstring text = L"拖入以剪切暂存";
    auto layout = CreateTextLayout(text, m_res->titleFormat, width - 60.0f);
    if (!layout) return;
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextLayout(D2D1::Point2F(iconX + iconSize + 12.0f, top + (height - metrics.height) / 2.0f), layout.Get(), m_res->whiteBrush);
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

    std::wstring countText = std::to_wstring(m_storedFiles.size()) + L" 个文件";
    auto countLayout = CreateTextLayout(countText, m_res->titleFormat, width - 86.0f);
    if (countLayout) {
        DWRITE_TEXT_METRICS metrics;
        countLayout->GetMetrics(&metrics);
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(left + 58.0f, top + (height - metrics.height) / 2.0f), countLayout.Get(), m_res->whiteBrush);
    }
}

void FilePanelComponent::RenderPreviewPane(const D2D1_RECT_F& rect, float contentAlpha) {
    if (m_storedFiles.empty() || m_selectedFileIndex < 0 || m_selectedFileIndex >= (int)m_storedFiles.size()) return;

    auto* ctx = m_res->d2dContext;
    const auto& item = m_storedFiles[m_selectedFileIndex];
    D2D1_RECT_F previewRect = D2D1::RectF(rect.left + 8.0f, rect.top + 8.0f, rect.right - 8.0f, rect.top + PREVIEW_HEIGHT);
    m_res->blackBrush->SetOpacity(0.18f * contentAlpha);
    D2D1_ROUNDED_RECT previewRoundedRect = D2D1::RoundedRect(previewRect, 12.0f, 12.0f);
    ctx->FillRoundedRectangle(&previewRoundedRect, m_res->blackBrush);

    float iconLeft = previewRect.left + 12.0f;
    float iconTop = previewRect.top + 12.0f;
    float iconSize = 38.0f;
    auto icon = GetFileIcon(item.stagedPath);
    if (icon) {
        ctx->DrawBitmap(icon.Get(), D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        m_res->fileBrush->SetOpacity(contentAlpha);
        D2D1_ROUNDED_RECT iconRect = D2D1::RoundedRect(D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize), 8.0f, 8.0f);
        ctx->FillRoundedRectangle(&iconRect, m_res->fileBrush);
    }

    float textLeft = iconLeft + iconSize + 12.0f;
    auto nameLayout = CreateTextLayout(item.displayName, m_res->titleFormat, previewRect.right - textLeft - 8.0f);
    if (nameLayout) {
        nameLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, previewRect.top + 10.0f), nameLayout.Get(), m_res->whiteBrush);
    }

    std::wstring meta = item.extension.empty() ? L"系统文件" : item.extension + L" · " + std::to_wstring((item.sizeBytes + 1023) / 1024) + L" KB";
    auto metaLayout = CreateTextLayout(meta, m_res->subFormat, previewRect.right - textLeft - 8.0f);
    if (metaLayout) {
        m_res->grayBrush->SetOpacity(0.80f * contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeft, previewRect.top + 34.0f), metaLayout.Get(), m_res->grayBrush);
    }
}

void FilePanelComponent::RenderExpandedView(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(rect, 18.0f, 18.0f);
    m_res->whiteBrush->SetOpacity(0.10f * contentAlpha);
    ctx->DrawRoundedRectangle(&panelRect, m_res->whiteBrush, 1.0f);

    RenderPreviewPane(rect, contentAlpha);

    float listTop = rect.top + PREVIEW_HEIGHT + 8.0f;
    for (size_t i = 0; i < m_storedFiles.size() && i < FileStashStore::kMaxItems; ++i) {
        float rowTop = listTop + (float)i * ITEM_HEIGHT;
        D2D1_RECT_F rowRect = D2D1::RectF(rect.left + 8.0f, rowTop, rect.right - 8.0f, rowTop + ITEM_HEIGHT - 2.0f);

        if ((int)i == m_selectedFileIndex) {
            m_res->fileBrush->SetOpacity(0.18f * contentAlpha);
            D2D1_ROUNDED_RECT rowRoundedRect = D2D1::RoundedRect(rowRect, 10.0f, 10.0f);
            ctx->FillRoundedRectangle(&rowRoundedRect, m_res->fileBrush);
        } else if ((int)i == m_hoveredFileIndex) {
            m_res->whiteBrush->SetOpacity(0.08f * contentAlpha);
            D2D1_ROUNDED_RECT rowRoundedRect = D2D1::RoundedRect(rowRect, 10.0f, 10.0f);
            ctx->FillRoundedRectangle(&rowRoundedRect, m_res->whiteBrush);
        }

        auto icon = GetFileIcon(m_storedFiles[i].stagedPath);
        float iconLeft = rowRect.left + 8.0f;
        float iconTop = rowRect.top + 4.0f;
        if (icon) {
            ctx->DrawBitmap(icon.Get(), D2D1::RectF(iconLeft, iconTop, iconLeft + 18.0f, iconTop + 18.0f), contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            m_res->fileBrush->SetOpacity(contentAlpha);
            D2D1_ROUNDED_RECT smallIconRect = D2D1::RoundedRect(D2D1::RectF(iconLeft, iconTop, iconLeft + 18.0f, iconTop + 18.0f), 4.0f, 4.0f);
            ctx->FillRoundedRectangle(&smallIconRect, m_res->fileBrush);
        }

        auto nameLayout = CreateTextLayout(m_storedFiles[i].displayName, m_res->subFormat, rowRect.right - iconLeft - 28.0f);
        if (nameLayout) {
            nameLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_res->whiteBrush->SetOpacity(contentAlpha);
            ctx->DrawTextLayout(D2D1::Point2F(iconLeft + 26.0f, rowRect.top + 4.0f), nameLayout.Get(), m_res->whiteBrush);
        }
    }
}

ComPtr<IDWriteTextLayout> FilePanelComponent::CreateTextLayout(const std::wstring& text, IDWriteTextFormat* format, float maxWidth) {
    if (!m_res || !m_res->dwriteFactory) return nullptr;
    ComPtr<IDWriteTextLayout> layout;
    m_res->dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(), format, maxWidth, 100.0f, &layout);
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
    if (m_iconCache.count(cacheKey)) {
        return m_iconCache[cacheKey];
    }

    SHFILEINFOW sfi = { 0 };
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
        if (sfi.hIcon) {
            ComPtr<IWICBitmap> wicBitmap;
            if (SUCCEEDED(m_res->wicFactory->CreateBitmapFromHICON(sfi.hIcon, &wicBitmap))) {
                ComPtr<ID2D1Bitmap> d2dBitmap;
                if (SUCCEEDED(m_res->d2dContext->CreateBitmapFromWicBitmap(wicBitmap.Get(), &d2dBitmap))) {
                    m_iconCache[cacheKey] = d2dBitmap;
                    DestroyIcon(sfi.hIcon);
                    return d2dBitmap;
                }
            }
            DestroyIcon(sfi.hIcon);
        }
    }

    return nullptr;
}
