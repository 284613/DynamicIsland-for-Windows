#include "FaceEnrollWindow.h"

#include "face_core/face_core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <windowsx.h>

using Microsoft::WRL::ComPtr;
using namespace face_core;

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr const wchar_t* kClassName = L"DI_FaceEnrollWindow";
constexpr UINT WM_ENROLL_PROGRESS = WM_APP + 61;
constexpr UINT WM_ENROLL_FINISHED = WM_APP + 62;
constexpr int kClientWidth = 720;
constexpr int kClientHeight = 700;
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;

double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

std::optional<FaceBox> BestFace(FaceDetector& detector, const Image& frame) {
    std::vector<FaceBox> faces = detector.Detect(frame);
    if (faces.empty()) {
        return std::nullopt;
    }
    return *std::max_element(faces.begin(), faces.end(),
        [](const FaceBox& a, const FaceBox& b) { return a.score < b.score; });
}

bool IsPoseForAngle(uint8_t tag, const HeadPose& pose) {
    switch (tag) {
    case 0: return std::fabs(pose.yaw) < 5.0f && std::fabs(pose.pitch) < 10.0f;
    case 1: return pose.yaw < -12.0f;
    case 2: return pose.yaw > 12.0f;
    default: return false;
    }
}

const wchar_t* StepTitle(int step) {
    switch (step) {
    case 0: return L"正视屏幕";
    case 1: return L"缓慢向左转头";
    case 2: return L"缓慢向右转头";
    default: return L"保存录入结果";
    }
}

uint8_t StepTag(int step) {
    return static_cast<uint8_t>((std::max)(0, (std::min)(2, step)));
}

D2D1_COLOR_F Color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r, g, b, a);
}

D2D1_POINT_2F PointOnCircle(float cx, float cy, float radius, float angle) {
    return D2D1::Point2F(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
}

D2D1_RECT_F CoverAspectRect(const D2D1_RECT_F& bounds, int srcWidth, int srcHeight) {
    if (srcWidth <= 0 || srcHeight <= 0) {
        return bounds;
    }
    const float boundsW = bounds.right - bounds.left;
    const float boundsH = bounds.bottom - bounds.top;
    const float srcRatio = static_cast<float>(srcWidth) / static_cast<float>(srcHeight);
    const float boundsRatio = boundsW / (std::max)(1.0f, boundsH);

    float drawW = boundsW;
    float drawH = boundsH;
    if (boundsRatio > srcRatio) {
        drawW = boundsW;
        drawH = drawW / srcRatio;
    } else {
        drawH = boundsH;
        drawW = drawH * srcRatio;
    }

    const float left = bounds.left + (boundsW - drawW) * 0.5f;
    const float top = bounds.top + (boundsH - drawH) * 0.5f;
    return D2D1::RectF(left, top, left + drawW, top + drawH);
}

void DrawSmile(ID2D1Factory1* factory, ID2D1RenderTarget* target, ID2D1Brush* brush,
    float cx, float cy, float width, float height, float strokeWidth) {
    if (!factory || !target || !brush) {
        return;
    }

    ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(factory->CreatePathGeometry(&geometry))) {
        return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink))) {
        return;
    }
    sink->BeginFigure(D2D1::Point2F(cx - width * 0.5f, cy), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(cx - width * 0.22f, cy + height),
        D2D1::Point2F(cx + width * 0.22f, cy + height),
        D2D1::Point2F(cx + width * 0.5f, cy)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close())) {
        target->DrawGeometry(geometry.Get(), brush, strokeWidth);
    }
}

void DrawArc(ID2D1Factory1* factory, ID2D1RenderTarget* target, ID2D1Brush* brush,
    float cx, float cy, float radius, float startAngle, float sweepAngle, float strokeWidth) {
    if (!factory || !target || !brush || sweepAngle <= 0.001f) {
        return;
    }

    if (sweepAngle >= kTwoPi - 0.01f) {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
        target->DrawEllipse(&ellipse, brush, strokeWidth);
        return;
    }

    ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(factory->CreatePathGeometry(&geometry))) {
        return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink))) {
        return;
    }
    D2D1_POINT_2F start = PointOnCircle(cx, cy, radius, startAngle);
    D2D1_POINT_2F end = PointOnCircle(cx, cy, radius, startAngle + sweepAngle);
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(
        end,
        D2D1::SizeF(radius, radius),
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        sweepAngle > kPi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close())) {
        target->DrawGeometry(geometry.Get(), brush, strokeWidth);
    }
}

std::vector<unsigned char> BgrToBgra(const Image& image) {
    std::vector<unsigned char> out;
    if (image.empty() || image.channels != 3) {
        return out;
    }
    out.resize(static_cast<size_t>(image.width) * image.height * 4);
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* src = image.ptr(y);
        unsigned char* dst = out.data() + static_cast<size_t>(y) * image.width * 4;
        for (int x = 0; x < image.width; ++x) {
            dst[x * 4 + 0] = src[x * 3 + 0];
            dst[x * 4 + 1] = src[x * 3 + 1];
            dst[x * 4 + 2] = src[x * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
    return out;
}

} // namespace

FaceEnrollWindow::FaceEnrollWindow() = default;

FaceEnrollWindow::~FaceEnrollWindow() {
    StopWorker();
}

FaceEnrollWindow::Result FaceEnrollWindow::ShowModal(HINSTANCE instance, HWND owner) {
    if (!instance) {
        instance = GetModuleHandleW(nullptr);
    }
    m_instance = instance;
    if (owner && !IsWindow(owner)) owner = nullptr;
    m_owner = owner;
    m_result = Result::Cancelled;
    m_cancel = false;
    m_done = false;

    WNDCLASSW wc{};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            wchar_t msg[256];
            swprintf_s(msg, L"RegisterClassW 失败，GetLastError=%lu", err);
            MessageBoxW(owner, msg, L"人脸录入窗口创建失败", MB_ICONERROR | MB_OK);
            return Result::Failed;
        }
    }

    const DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT desiredRect{ 0, 0, kClientWidth, kClientHeight };
    AdjustWindowRectEx(&desiredRect, windowStyle, FALSE, 0);
    const int width = desiredRect.right - desiredRect.left;
    const int height = desiredRect.bottom - desiredRect.top;
    int x = 0;
    int y = 0;
    if (owner) {
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    }

    m_hwnd = CreateWindowExW(
        0,
        kClassName,
        L"录入人脸",
        windowStyle,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        this);

    if (!m_hwnd) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        swprintf_s(msg, L"CreateWindowExW 失败，GetLastError=%lu", err);
        MessageBoxW(owner, msg, L"人脸录入窗口创建失败", MB_ICONERROR | MB_OK);
        return Result::Failed;
    }

    if (owner) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    StartWorker();

    MSG msg{};
    while (IsWindow(m_hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_done && !IsWindow(m_hwnd)) {
            break;
        }
    }

    StopWorker();
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return m_result;
}

LRESULT CALLBACK FaceEnrollWindow::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    FaceEnrollWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<FaceEnrollWindow*>(cs->lpCreateParams);
        if (!self) {
            return FALSE;
        }
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<FaceEnrollWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->WndProc(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT FaceEnrollWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowResources();
        return 0;
    case WM_SIZE:
        if (m_renderTarget) {
            m_renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            m_renderTarget->SetDpi(96.0f, 96.0f);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        Render();
        ValidateRect(m_hwnd, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_ENROLL_PROGRESS:
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_ENROLL_FINISHED:
        DestroyWindow(m_hwnd);
        return 0;
    case WM_CLOSE:
        if (!m_done) {
            m_cancel = true;
            m_result = Result::Cancelled;
        }
        DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_NCDESTROY:
        if (m_hwnd == hwnd) {
            m_hwnd = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool FaceEnrollWindow::CreateWindowResources() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1), reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    m_dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"zh-cn", &m_titleFormat);
    m_dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-cn", &m_bodyFormat);
    m_dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-cn", &m_captionFormat);

    if (m_titleFormat) {
        m_titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (m_bodyFormat) {
        m_bodyFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_bodyFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (m_captionFormat) {
        m_captionFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_captionFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return EnsureRenderTarget();
}

bool FaceEnrollWindow::EnsureRenderTarget() {
    if (m_renderTarget) {
        return true;
    }
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(
        m_hwnd,
        D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)));
    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(rtp, hrtp, &m_renderTarget);
    if (FAILED(hr)) {
        return false;
    }
    m_renderTarget->SetDpi(96.0f, 96.0f);
    m_renderTarget->CreateSolidColorBrush(Color(1, 1, 1), &m_brush);
    return true;
}

void FaceEnrollWindow::DiscardRenderTarget() {
    m_brush.Reset();
    m_renderTarget.Reset();
}

void FaceEnrollWindow::Render() {
    if (!EnsureRenderTarget()) {
        return;
    }

    PAINTSTRUCT ps{};
    BeginPaint(m_hwnd, &ps);

    std::wstring status;
    int step = 0;
    int total = 0;
    int previewW = 0;
    int previewH = 0;
    std::vector<unsigned char> preview;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        status = m_status;
        step = m_step;
        total = m_totalCaptured;
        previewW = m_previewWidth;
        previewH = m_previewHeight;
        preview = m_previewBgra;
    }

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(Color(0.010f, 0.011f, 0.014f));

    auto drawText = [&](const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
        if (!format) return;
        m_brush->SetColor(color);
        m_renderTarget->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, m_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    drawText(L"录入人脸", m_titleFormat.Get(), D2D1::RectF(0, 24, kClientWidth, 58), Color(0.96f, 0.96f, 0.98f));
    drawText(StepTitle(step), m_bodyFormat.Get(), D2D1::RectF(0, 60, kClientWidth, 88), Color(0.72f, 0.74f, 0.80f));

    const float cx = kClientWidth * 0.5f;
    const float cy = 318.0f;
    const float previewRadius = 202.0f;
    const float ringRadius = 218.0f;
    const D2D1_RECT_F circleRect = D2D1::RectF(
        cx - previewRadius,
        cy - previewRadius,
        cx + previewRadius,
        cy + previewRadius);

    m_brush->SetColor(Color(0.025f, 0.027f, 0.034f));
    D2D1_ELLIPSE previewEllipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), previewRadius, previewRadius);
    m_renderTarget->FillEllipse(&previewEllipse, m_brush.Get());

    if (!preview.empty() && previewW > 0 && previewH > 0) {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        ComPtr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(m_renderTarget->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(previewW), static_cast<UINT32>(previewH)),
            preview.data(),
            static_cast<UINT32>(previewW * 4),
            props,
            &bitmap))) {
            ComPtr<ID2D1EllipseGeometry> clipGeometry;
            if (SUCCEEDED(m_d2dFactory->CreateEllipseGeometry(&previewEllipse, &clipGeometry))) {
                ComPtr<ID2D1Layer> clipLayer;
                m_renderTarget->CreateLayer(nullptr, &clipLayer);
                D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeometry.Get());
                m_renderTarget->PushLayer(&layerParams, clipLayer.Get());
                m_renderTarget->DrawBitmap(
                    bitmap.Get(),
                    CoverAspectRect(circleRect, previewW, previewH),
                    0.96f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                m_renderTarget->PopLayer();
            }
        }
    } else {
        drawText(L"正在打开摄像头...", m_bodyFormat.Get(),
            D2D1::RectF(circleRect.left, cy - 15.0f, circleRect.right, cy + 18.0f),
            Color(0.64f, 0.66f, 0.72f));
        m_brush->SetColor(Color(1.0f, 1.0f, 1.0f, 0.42f));
        const D2D1_RECT_F faceRect = D2D1::RectF(cx - 56.0f, cy - 86.0f, cx + 56.0f, cy + 26.0f);
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(faceRect, 24.0f, 24.0f), m_brush.Get(), 2.0f);
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 24.0f, cy - 42.0f), 4.0f, 4.0f), m_brush.Get());
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 24.0f, cy - 42.0f), 4.0f, 4.0f), m_brush.Get());
        DrawSmile(m_d2dFactory.Get(), m_renderTarget.Get(), m_brush.Get(), cx, cy - 5.0f, 56.0f, 22.0f, 2.0f);
    }

    m_brush->SetColor(Color(1.0f, 1.0f, 1.0f, 0.18f));
    m_renderTarget->DrawEllipse(&previewEllipse, m_brush.Get(), 1.2f);

    D2D1_ELLIPSE ringEllipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), ringRadius, ringRadius);
    m_brush->SetColor(Color(1.0f, 1.0f, 1.0f, 0.13f));
    m_renderTarget->DrawEllipse(&ringEllipse, m_brush.Get(), 7.0f);

    const float progress = Clamp01(static_cast<float>(total) / 6.0f);
    m_brush->SetColor(Color(0.22f, 0.64f, 1.0f, 0.96f));
    DrawArc(m_d2dFactory.Get(), m_renderTarget.Get(), m_brush.Get(),
        cx, cy, ringRadius, -kPi * 0.5f, kTwoPi * progress, 7.0f);
    if (progress > 0.0f) {
        const D2D1_POINT_2F dot = PointOnCircle(cx, cy, ringRadius, -kPi * 0.5f + kTwoPi * progress);
        m_renderTarget->FillEllipse(D2D1::Ellipse(dot, 4.0f, 4.0f), m_brush.Get());
    }

    const std::wstring hint = status.empty() ? L"请把脸放入圆圈内" : status;
    drawText(hint, m_bodyFormat.Get(), D2D1::RectF(58, 558, 662, 590), Color(0.94f, 0.95f, 0.98f));

    wchar_t progressText[64] = {};
    swprintf_s(progressText, L"已采集 %d / 6", total);
    drawText(progressText, m_captionFormat.Get(), D2D1::RectF(58, 594, 662, 618), Color(0.55f, 0.58f, 0.66f));
    drawText(L"按提示缓慢调整头部方向", m_captionFormat.Get(), D2D1::RectF(58, 622, 662, 648), Color(0.42f, 0.45f, 0.52f));

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardRenderTarget();
    }

    EndPaint(m_hwnd, &ps);
}

void FaceEnrollWindow::StartWorker() {
    m_worker = std::thread([this]() { WorkerMain(); });
}

void FaceEnrollWindow::StopWorker() {
    m_cancel = true;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void FaceEnrollWindow::SetStatus(const std::wstring& status) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = status;
    }
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_ENROLL_PROGRESS, 0, 0);
    }
}

void FaceEnrollWindow::SetPreviewFrame(int width, int height, const std::vector<unsigned char>& bgra) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_previewWidth = width;
        m_previewHeight = height;
        m_previewBgra = bgra;
    }
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_ENROLL_PROGRESS, 0, 0);
    }
}

std::string FaceEnrollWindow::EnrollmentName() const {
    wchar_t name[256] = {};
    DWORD size = static_cast<DWORD>(_countof(name));
    if (!GetUserNameW(name, &size) || size <= 1) {
        return "local_user";
    }

    std::string ascii;
    for (const wchar_t* p = name; *p; ++p) {
        if (*p < 32 || *p > 126) {
            return "local_user";
        }
        ascii.push_back(static_cast<char>(*p));
    }
    if (ascii.empty() || ascii.size() > 32) {
        return "local_user";
    }
    return ascii;
}

void FaceEnrollWindow::WorkerMain() {
    try {
        const std::string name = EnrollmentName();

        FaceTemplateStore store;
        if (!store.Load()) {
            throw std::runtime_error("could not load face template store");
        }
        store.Remove(name);

        CameraCapture camera;
        SetStatus(L"正在打开摄像头...");
        if (!camera.Open(800, 600)) {
            throw std::runtime_error("could not open camera");
        }

        FaceDetector detector(0.6f);
        LivenessDetector liveness;
        FaceLandmarks3D landmarks;
        FaceRecognizer recognizer;

        for (int step = 0; step < 3 && !m_cancel; ++step) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_step = step;
                m_capturedInStep = 0;
            }
            SetStatus(StepTitle(step));

            int captured = 0;
            double lastCapture = 0.0;
            while (captured < 2 && !m_cancel) {
                Image frame = camera.ReadFrame();
                if (frame.empty()) {
                    throw std::runtime_error("camera frame read failed");
                }
                SetPreviewFrame(frame.width, frame.height, BgrToBgra(frame));

                auto best = BestFace(detector, frame);
                if (!best) {
                    SetStatus(L"请把脸放入圆圈内");
                    continue;
                }

                float real = liveness.Score(frame, *best);
                std::array<float, 62> params{};
                if (real < LivenessDetector::kDefaultRealThreshold || !landmarks.Run(frame, *best, params)) {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_liveScore = real;
                    }
                    SetStatus(L"保持注视，等待真人检测通过");
                    continue;
                }

                HeadPose pose = FaceLandmarks3D::ExtractPose(params);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_liveScore = real;
                    m_yaw = pose.yaw;
                    m_pitch = pose.pitch;
                }

                if (!IsPoseForAngle(StepTag(step), pose)) {
                    SetStatus(StepTitle(step));
                    continue;
                }

                double now = NowSeconds();
                if (now - lastCapture < 0.25) {
                    continue;
                }

                Image aligned = AlignArcFace(frame, best->kps);
                Embedding embedding{};
                if (aligned.empty() || !recognizer.Embed(aligned, embedding)) {
                    SetStatus(L"人脸对齐失败，请稍微调整位置");
                    continue;
                }

                store.Add(FaceTemplate{ name, StepTag(step), embedding });
                lastCapture = now;
                ++captured;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_capturedInStep = captured;
                    m_totalCaptured = step * 2 + captured;
                    m_status = L"已采集，继续下一步";
                }
                if (m_hwnd) {
                    PostMessageW(m_hwnd, WM_ENROLL_PROGRESS, 0, 0);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }

        if (m_cancel) {
            m_result = Result::Cancelled;
        } else {
            store.Save();
            try {
                std::wstring src = store.Path();
                std::wstring dst = FaceTemplateStore::SharedPath();
                if (!src.empty() && !dst.empty() && src != dst) {
                    std::filesystem::copy_file(src, dst,
                        std::filesystem::copy_options::overwrite_existing);
                }
            } catch (...) {}
            SetStatus(L"录入完成");
            m_result = Result::Completed;
        }
    } catch (const std::exception& ex) {
        std::string what = ex.what();
        std::wstring message = L"录入失败: " + std::wstring(what.begin(), what.end());
        SetStatus(message);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_errorMessage = message;
        }
        m_result = Result::Failed;
        MessageBoxW(m_owner, message.c_str(), L"人脸录入失败", MB_ICONERROR | MB_OK);
    } catch (...) {
        std::wstring message = L"录入失败: 未知异常";
        SetStatus(message);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_errorMessage = message;
        }
        m_result = Result::Failed;
        MessageBoxW(m_owner, message.c_str(), L"人脸录入失败", MB_ICONERROR | MB_OK);
    }

    m_done = true;
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_ENROLL_FINISHED, 0, 0);
    }
}
