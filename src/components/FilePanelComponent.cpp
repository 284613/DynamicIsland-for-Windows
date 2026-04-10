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

void FilePanelComponent::SetDragHovering(bool hovering) {
    m_isDragHovering = hovering;
}

void FilePanelComponent::SetStoredFiles(size_t count, const std::vector<std::wstring>& files) {
    m_storedFileCount = count;
    m_storedFiles = files;
}

void FilePanelComponent::SetHoverState(int hoveredIndex, bool isDeleteHovered) {
    m_hoveredFileIndex = hoveredIndex;
    m_isFileDeleteHovered = isDeleteHovered;
}

void FilePanelComponent::Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG) {
    if (!m_res || !m_res->d2dContext) return;

    float height = rect.bottom - rect.top;
    float width = rect.right - rect.left;

    if (contentAlpha <= 0.01f) return;

    if (m_isDragHovering) {
        RenderDragHint(rect, contentAlpha);
    } else if (m_storedFileCount > 0) {
        if (height < 60.0f) {
            RenderCompactView(rect, contentAlpha);
        } else {
            RenderExpandedView(rect, contentAlpha);
        }
    }
}

void FilePanelComponent::RenderDragHint(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;

    float iconSize = 28.0f;
    float startX = left + (width - iconSize) / 2.0f;
    float startY = top + (height - iconSize) / 2.0f - 12.0f;

    // Draw down arrow icon
    m_res->themeBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(L"\uE8E5", 1, m_res->iconFormat,
        D2D1::RectF(startX, startY, startX + iconSize, startY + iconSize), m_res->themeBrush);

    // Draw hint text
    std::wstring text = L"松开以暂存文件";
    auto textLayout = CreateTextLayout(text, m_res->subFormat, 200.0f);
    if (!textLayout) return;

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextLayout(D2D1::Point2F(left + (width - metrics.width) / 2.0f, startY + 35.0f), textLayout.Get(), m_res->whiteBrush);
}

void FilePanelComponent::RenderCompactView(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;

    float iconSize = 18.0f;
    std::wstring text = L"已暂存 " + std::to_wstring(m_storedFileCount) + L" 个文件";

    auto textLayout = CreateTextLayout(text, m_res->titleFormat, 100.0f);
    if (!textLayout) return;

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    float totalWidth = iconSize + 8.0f + metrics.width;
    float startX = left + (width - totalWidth) / 2.0f;
    float textY = top + (height - metrics.height) / 2.0f;

    // Draw folder icon
    m_res->themeBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(L"\uE8B7", 1, m_res->iconFormat,
        D2D1::RectF(startX, top + (height - iconSize) / 2.0f, startX + iconSize, top + (height - iconSize) / 2.0f + iconSize), m_res->themeBrush);

    m_res->whiteBrush->SetOpacity(contentAlpha);
    ctx->DrawTextLayout(D2D1::Point2F(startX + iconSize + 8.0f, textY), textLayout.Get(), m_res->whiteBrush);
}

void FilePanelComponent::RenderExpandedView(const D2D1_RECT_F& rect, float contentAlpha) {
    auto* ctx = m_res->d2dContext;
    float left = rect.left;
    float top = rect.top;
    float width = rect.right - rect.left;

    float artSize = 60.0f;
    float artLeft = left + 20.0f;
    float artTop = top + 30.0f;

    D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), 12.0f, 12.0f);

    // Dark gray background
    m_res->darkGrayBrush->SetOpacity(contentAlpha);
    ctx->FillRoundedRectangle(&artRect, m_res->darkGrayBrush);

    // Folder icon
    m_res->themeBrush->SetOpacity(contentAlpha);
    ctx->DrawTextW(L"\uE8B7", 1, m_res->iconFormat,
        D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), m_res->themeBrush);

    // Text
    float textLeftFile = artLeft + artSize + 15.0f;
    std::wstring textTop = L"文件暂存区";
    std::wstring textBot = std::to_wstring(m_storedFileCount) + L" 个文件 (点击全部打开)";

    auto topLayout = CreateTextLayout(textTop, m_res->subFormat, 200.0f);
    auto botLayout = CreateTextLayout(textBot, m_res->titleFormat, 200.0f);

    if (topLayout) {
        m_res->grayBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeftFile, artTop + 5.0f), topLayout.Get(), m_res->grayBrush);
    }
    if (botLayout) {
        m_res->whiteBrush->SetOpacity(contentAlpha);
        ctx->DrawTextLayout(D2D1::Point2F(textLeftFile, artTop + 28.0f), botLayout.Get(), m_res->whiteBrush);
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

    SHFILEINFOW sfi = {0};
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
