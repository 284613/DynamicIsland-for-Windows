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

void FilePanelComponent::SetD2DResources(
    ComPtr<ID2D1DeviceContext> d2dContext,
    ComPtr<IDWriteFactory> dwriteFactory,
    ComPtr<ID2D1Factory1> d2dFactory) {
    m_d2dContext = d2dContext;
    m_dwriteFactory = dwriteFactory;
    m_d2dFactory = d2dFactory;
}

void FilePanelComponent::SetTextFormats(
    ComPtr<IDWriteTextFormat> titleFormat,
    ComPtr<IDWriteTextFormat> subFormat,
    ComPtr<IDWriteTextFormat> iconFormat,
    ComPtr<IDWriteTextFormat> iconTextFormat) {
    m_titleFormat = titleFormat;
    m_subFormat = subFormat;
    m_iconFormat = iconFormat;
    m_iconTextFormat = iconTextFormat;
}

void FilePanelComponent::SetBrushes(
    ComPtr<ID2D1SolidColorBrush> whiteBrush,
    ComPtr<ID2D1SolidColorBrush> grayBrush,
    ComPtr<ID2D1SolidColorBrush> themeBrush,
    ComPtr<ID2D1SolidColorBrush> buttonHoverBrush,
    ComPtr<ID2D1SolidColorBrush> fileBrush) {
    m_whiteBrush = whiteBrush;
    m_grayBrush = grayBrush;
    m_themeBrush = themeBrush;
    m_buttonHoverBrush = buttonHoverBrush;
    m_fileBrush = fileBrush;
}

void FilePanelComponent::SetWicFactory(ComPtr<IWICImagingFactory> wicFactory) {
    m_wicFactory = wicFactory;
}

void FilePanelComponent::Draw(ID2D1DeviceContext* ctx, float left, float top, float width, float height,
                             const RenderContext& ctx_data, float dpi) {
    if (ctx_data.mode != IslandDisplayMode::FileDrop && ctx_data.storedFiles.empty()) return;

    float alpha = ctx_data.contentAlpha;
    if (alpha <= 0.01f) return;

    // 根据高度判断：40px 左右显示紧凑，100px+ 显示列表
    if (height < 60.0f) {
        RenderCompactView(left, top, width, height, ctx_data);
    } else {
        RenderFileList(left, top, width, height, ctx_data);
    }
}

void FilePanelComponent::RenderCompactView(float left, float top, float width, float height, const RenderContext& ctx_data) {
    float iconSize = 20.0f;
    std::wstring text = std::to_wstring(ctx_data.storedFiles.size());
    
    // 获取文字布局并测量宽度
    auto textLayout = CreateTextLayout(text, m_subFormat.Get(), 100.0f);
    if (!textLayout) return;

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    float spacing = 6.0f;
    float totalContentWidth = iconSize + spacing + metrics.width;
    
    // 计算居中起始位置
    float startX = left + (width - totalContentWidth) / 2.0f;
    float contentY = top + (height - iconSize) / 2.0f;

    // 1. 绘制图标
    m_themeBrush->SetOpacity(ctx_data.contentAlpha);
    D2D1_RECT_F iconRect = D2D1::RectF(
        std::floor(startX), 
        std::floor(contentY), 
        std::floor(startX + iconSize), 
        std::floor(contentY + iconSize)
    );
    m_d2dContext->DrawTextW(L"\xE8B7", 1, m_iconFormat.Get(), iconRect, m_themeBrush.Get());

    // 2. 绘制数字 (垂直居中)
    m_whiteBrush->SetOpacity(ctx_data.contentAlpha);
    float textY = top + (height - metrics.height) / 2.0f;
    m_d2dContext->DrawTextLayout(
        D2D1::Point2F(std::floor(startX + iconSize + spacing), std::floor(textY)),
        textLayout.Get(),
        m_whiteBrush.Get()
    );
}

ComPtr<IDWriteTextLayout> FilePanelComponent::CreateTextLayout(const std::wstring& text, IDWriteTextFormat* format, float maxWidth) {
    ComPtr<IDWriteTextLayout> layout;
    m_dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(), format, maxWidth, 100.0f, &layout);
    return layout;
}

void FilePanelComponent::RenderFileList(float left, float top, float width, float height, const RenderContext& ctx_data) {
    float currentY = std::floor(top + PADDING + 5.0f);
    float contentWidth = width - (PADDING * 2);

    for (size_t i = 0; i < ctx_data.storedFiles.size(); ++i) {
        bool isHovered = (ctx_data.hoveredFileIndex == (int)i);
        bool isDelHovered = isHovered && ctx_data.isFileDeleteHovered;

        DrawFileItem(std::floor(left + PADDING), currentY, contentWidth, ctx_data.storedFiles[i], 
                     isHovered, isDelHovered, ctx_data.contentAlpha);
        
        currentY += ITEM_HEIGHT + 4.0f;
        
        // Ensure clipping is not too aggressive
        if (currentY > top + height + 20.0f) break;
    }
}

void FilePanelComponent::DrawFileItem(float x, float y, float width, const std::wstring& path, 
                                     bool hovered, bool deleteHovered, float alpha) {
    D2D1_RECT_F itemRect = D2D1::RectF(x, y, x + width, y + ITEM_HEIGHT);
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(itemRect, 8.0f, 8.0f);

    // 1. Draw Background
    if (hovered) {
        m_buttonHoverBrush->SetOpacity(0.3f * alpha);
        m_d2dContext->FillRoundedRectangle(&roundedRect, m_buttonHoverBrush.Get());
    } else {
        m_grayBrush->SetOpacity(0.1f * alpha);
        m_d2dContext->FillRoundedRectangle(&roundedRect, m_grayBrush.Get());
    }

    // 2. Draw Icon
    ComPtr<ID2D1Bitmap> icon = GetFileIcon(path);
    if (icon) {
        float iconY = std::floor(y + (ITEM_HEIGHT - ICON_SIZE) / 2.0f);
        D2D1_RECT_F iconRect = D2D1::RectF(x + 8.0f, iconY, x + 8.0f + ICON_SIZE, iconY + ICON_SIZE);
        m_d2dContext->DrawBitmap(icon.Get(), iconRect, alpha);
    }

    // 3. Draw Filename
    std::wstring filename = path;
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        filename = path.substr(lastSlash + 1);
    }

    D2D1_RECT_F textRect = D2D1::RectF(
        std::floor(x + 8.0f + ICON_SIZE + 10.0f),
        std::floor(y),
        std::floor(x + width - DELETE_BTN_SIZE - 15.0f),
        std::floor(y + ITEM_HEIGHT)
    );

    m_whiteBrush->SetOpacity(alpha);
    m_d2dContext->DrawText(
        filename.c_str(), 
        (UINT32)filename.length(), 
        m_subFormat.Get(), 
        textRect, 
        m_whiteBrush.Get(),
        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP
    );

    // 4. Draw Delete Button (X) \xE8BB
    float delX = std::floor(x + width - DELETE_BTN_SIZE - 8.0f);
    float delY = std::floor(y + (ITEM_HEIGHT - DELETE_BTN_SIZE) / 2.0f);
    D2D1_RECT_F delRect = D2D1::RectF(delX, delY, delX + DELETE_BTN_SIZE, delY + DELETE_BTN_SIZE);
    
    if (deleteHovered) {
        ComPtr<ID2D1SolidColorBrush> redBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 0.6f * alpha), &redBrush);
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(delX + DELETE_BTN_SIZE/2, delY + DELETE_BTN_SIZE/2), 
                                  DELETE_BTN_SIZE/2, DELETE_BTN_SIZE/2), redBrush.Get());
    }

    m_whiteBrush->SetOpacity(deleteHovered ? alpha : 0.4f * alpha);
    m_d2dContext->DrawText(L"\xE8BB", 1, m_iconFormat.Get(), delRect, m_whiteBrush.Get());
}

ComPtr<ID2D1Bitmap> FilePanelComponent::GetFileIcon(const std::wstring& path) {
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
            if (SUCCEEDED(m_wicFactory->CreateBitmapFromHICON(sfi.hIcon, &wicBitmap))) {
                ComPtr<ID2D1Bitmap> d2dBitmap;
                if (SUCCEEDED(m_d2dContext->CreateBitmapFromWicBitmap(wicBitmap.Get(), &d2dBitmap))) {
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
