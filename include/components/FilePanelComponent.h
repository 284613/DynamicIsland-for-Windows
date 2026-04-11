// FilePanelComponent.h
#pragma once
#include "IIslandComponent.h"
#include "FileStashStore.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class FilePanelComponent : public IIslandComponent {
public:
    enum class ViewMode {
        Hidden,
        Mini,
        Expanded,
        DropTarget
    };

    struct HitResult {
        enum class Kind {
            None,
            MiniBody,
            PreviewPane,
            CollapseButton,
            ExpandedBackground,
            FileItem
        };

        Kind kind = Kind::None;
        int index = -1;
    };

    FilePanelComponent();
    ~FilePanelComponent();

    bool Initialize();

    void OnAttach(SharedResources* res) override;
    void OnResize(float dpi, int width, int height) override {}
    void Update(float deltaTime) override {}
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_viewMode != ViewMode::Hidden; }
    bool NeedsRender() const override { return false; }

    void SetViewMode(ViewMode mode) { m_viewMode = mode; }
    void SetStoredFiles(const std::vector<FileStashItem>& files);
    void SetInteractionState(int selectedIndex, int hoveredIndex);
    void SetDpi(float dpi) { m_dpi = dpi; }
    HitResult HitTest(float x, float y) const;
    bool ContainsPoint(float x, float y) const;

private:
    bool HasSameStoredFiles(const std::vector<FileStashItem>& files) const;
    void ClearTextLayoutCache();
    float GetExpansionProgress(const D2D1_RECT_F& rect) const;
    D2D1_RECT_F GetPreviewRect(const D2D1_RECT_F& rect) const;
    D2D1_RECT_F GetCollapseButtonRect(const D2D1_RECT_F& rect) const;
    D2D1_RECT_F GetRowRect(const D2D1_RECT_F& rect, int index) const;
    void RenderDragHint(const D2D1_RECT_F& rect, float contentAlpha);
    void RenderCompactView(const D2D1_RECT_F& rect, float contentAlpha);
    void RenderExpandedView(const D2D1_RECT_F& rect, float contentAlpha, float expansionProgress);
    void RenderPreviewPane(const D2D1_RECT_F& rect, float contentAlpha, float revealProgress);

    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text, IDWriteTextFormat* format, float maxWidth);
    ComPtr<ID2D1Bitmap> GetFileIcon(const std::wstring& path);

    SharedResources* m_res = nullptr;
    ViewMode m_viewMode = ViewMode::Hidden;
    std::vector<FileStashItem> m_storedFiles;
    int m_hoveredFileIndex = -1;
    int m_selectedFileIndex = -1;
    float m_dpi = 96.0f;
    D2D1_RECT_F m_lastRect = D2D1::RectF(0, 0, 0, 0);

    std::map<std::wstring, ComPtr<ID2D1Bitmap>> m_iconCache;
    std::map<std::wstring, ComPtr<IDWriteTextLayout>> m_textLayoutCache;

    static constexpr float ITEM_HEIGHT = 22.0f;
    static constexpr float PREVIEW_HEIGHT = 62.0f;
    static constexpr float ICON_SIZE = 22.0f;
    static constexpr float PADDING = 12.0f;
};
