// FilePanelComponent.h
#pragma once
#include "IIslandComponent.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class FilePanelComponent : public IIslandComponent {
public:
    FilePanelComponent();
    ~FilePanelComponent();

    bool Initialize();

    // IIslandComponent implementation
    void OnAttach(SharedResources* res) override;
    void OnResize(float dpi, int width, int height) override {}
    void Update(float deltaTime) override {}
    void Draw(const D2D1_RECT_F& rect, float contentAlpha, ULONGLONG currentTimeMs) override;
    bool IsActive() const override { return m_isDragHovering || m_storedFileCount > 0; }
    bool NeedsRender() const override { return false; }

    // State setters (called by RenderEngine before Draw)
    void SetDragHovering(bool hovering);
    void SetStoredFiles(size_t count, const std::vector<std::wstring>& files);
    void SetHoverState(int hoveredIndex, bool isDeleteHovered);
    void SetDpi(float dpi) { m_dpi = dpi; }

private:
    void RenderDragHint(const D2D1_RECT_F& rect, float contentAlpha);
    void RenderCompactView(const D2D1_RECT_F& rect, float contentAlpha);
    void RenderExpandedView(const D2D1_RECT_F& rect, float contentAlpha);

    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text, IDWriteTextFormat* format, float maxWidth);
    ComPtr<ID2D1Bitmap> GetFileIcon(const std::wstring& path);

    // Shared resources
    SharedResources* m_res = nullptr;

    // State
    bool m_isDragHovering = false;
    size_t m_storedFileCount = 0;
    std::vector<std::wstring> m_storedFiles;
    int m_hoveredFileIndex = -1;
    bool m_isFileDeleteHovered = false;
    float m_dpi = 96.0f;

    // Icon cache
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> m_iconCache;

    // Layout constants
    static constexpr float ITEM_HEIGHT = 40.0f;
    static constexpr float ICON_SIZE = 22.0f;
    static constexpr float DELETE_BTN_SIZE = 22.0f;
    static constexpr float PADDING = 12.0f;
};
