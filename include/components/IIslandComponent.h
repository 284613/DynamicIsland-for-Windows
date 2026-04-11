#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>
#include <wincodec.h>

using namespace Microsoft::WRL;

// 所有组件共享的 D2D 资源（由 RenderEngine 初始化后填充）
struct SharedResources {
    // D2D 核心
    ID2D1DeviceContext* d2dContext    = nullptr;
    IDWriteFactory*     dwriteFactory = nullptr;
    ID2D1Factory1*      d2dFactory    = nullptr;

    // 公共画刷（组件直接复用，无需自建）
    ID2D1SolidColorBrush* whiteBrush  = nullptr;
    ID2D1SolidColorBrush* grayBrush   = nullptr;
    ID2D1SolidColorBrush* blackBrush  = nullptr;
    ID2D1SolidColorBrush* themeBrush  = nullptr;

    // 通知/状态提示画刷（AlertComponent 使用）
    ID2D1SolidColorBrush* wifiBrush         = nullptr;
    ID2D1SolidColorBrush* bluetoothBrush    = nullptr;
    ID2D1SolidColorBrush* chargingBrush     = nullptr;
    ID2D1SolidColorBrush* lowBatteryBrush   = nullptr;
    ID2D1SolidColorBrush* fileBrush         = nullptr;
    ID2D1SolidColorBrush* notificationBrush = nullptr;
    ID2D1SolidColorBrush* darkGrayBrush     = nullptr;

    // 音乐播放器画刷（MusicPlayerComponent 使用）
    ID2D1SolidColorBrush* progressBgBrush  = nullptr;
    ID2D1SolidColorBrush* progressFgBrush  = nullptr;
    ID2D1SolidColorBrush* buttonHoverBrush = nullptr;

    // 公共字体格式
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* subFormat   = nullptr;
    IDWriteTextFormat* iconFormat  = nullptr;

    // WIC 工厂（MusicPlayerComponent 加载专辑封面用）
    IWICImagingFactory* wicFactory = nullptr;
};

// 所有岛屿组件的统一接口
class IIslandComponent {
public:
    virtual ~IIslandComponent() = default;

    // ── 生命周期 ──────────────────────────────────────────────────────
    // RenderEngine::Initialize() 完成后调用一次，组件在此创建私有 D2D 资源
    virtual void OnAttach(SharedResources* res) = 0;

    // 窗口 DPI / 尺寸变化时调用，组件按需重建像素相关资源
    virtual void OnResize(float dpi, int width, int height) {}

    // ── 每帧调用 ──────────────────────────────────────────────────────
    // 推进动画状态（偏移、物理、相位），不做任何绘制
    virtual void Update(float deltaTime) {}

    // 在给定矩形内绘制；contentAlpha 由引擎统一计算
    virtual void Draw(const D2D1_RECT_F& rect,
                      float contentAlpha,
                      ULONGLONG currentTimeMs) = 0;

    // ── 查询 ──────────────────────────────────────────────────────────
    // 当前组件是否处于激活状态（用于优先级调度）
    virtual bool IsActive() const { return false; }

    // 是否需要持续渲染（有动画进行中，阻止引擎进入空闲）
    virtual bool NeedsRender() const { return false; }

    // ── 输入（可选重写）──────────────────────────────────────────────
    // 返回 true 表示已消费，引擎不再向下传递
    virtual bool OnMouseWheel(float x, float y, int delta) { return false; }
    virtual bool OnMouseMove(float x, float y)             { return false; }
    virtual bool OnMouseClick(float x, float y)            { return false; }
    virtual bool OnChar(wchar_t ch)                        { return false; }
    virtual bool OnKeyDown(WPARAM key)                     { return false; }
    virtual bool OnImeComposition(HWND hwnd, LPARAM lParam){ return false; }
    virtual bool OnImeSetContext(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result) {
        (void)hwnd;
        (void)wParam;
        (void)lParam;
        (void)result;
        return false;
    }
    virtual D2D1_RECT_F GetImeAnchorRect() const           { return D2D1::RectF(0, 0, 0, 0); }
};
