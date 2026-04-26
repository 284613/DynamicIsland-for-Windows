#include "FaceEnrollWindow.h"

#include "face_core/face_core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
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
    case 0: return L"正面对准摄像头";
    case 1: return L"微微向左转头";
    case 2: return L"微微向右转头";
    default: return L"保存录入结果";
    }
}

uint8_t StepTag(int step) {
    return static_cast<uint8_t>((std::max)(0, (std::min)(2, step)));
}

D2D1_COLOR_F Color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r, g, b, a);
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
    m_instance = instance;
    m_owner = owner;
    m_result = Result::Cancelled;
    m_cancel = false;
    m_done = false;

    WNDCLASSW wc{};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const int width = 620;
    const int height = 560;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        L"录入人脸",
        WS_CAPTION | WS_SYSMENU,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        this);

    if (!m_hwnd) {
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<FaceEnrollWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->WndProc(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT FaceEnrollWindow::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowResources();
        return 0;
    case WM_SIZE:
        if (m_renderTarget) {
            m_renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
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
        m_hwnd = nullptr;
        return 0;
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
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
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-cn", &m_bodyFormat);
    m_dwriteFactory->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-cn", &m_captionFormat);
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
    int captured = 0;
    int total = 0;
    float live = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int previewW = 0;
    int previewH = 0;
    std::vector<unsigned char> preview;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        status = m_status;
        step = m_step;
        captured = m_capturedInStep;
        total = m_totalCaptured;
        live = m_liveScore;
        yaw = m_yaw;
        pitch = m_pitch;
        previewW = m_previewWidth;
        previewH = m_previewHeight;
        preview = m_previewBgra;
    }

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(Color(0.07f, 0.075f, 0.09f));

    auto drawText = [&](const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
        m_brush->SetColor(color);
        m_renderTarget->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, m_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto fillRound = [&](const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color) {
        m_brush->SetColor(color);
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_brush.Get());
    };
    auto drawRound = [&](const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float stroke) {
        m_brush->SetColor(color);
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_brush.Get(), stroke);
    };

    drawText(L"人脸录入", m_titleFormat.Get(), D2D1::RectF(28, 20, 560, 56), Color(0.96f, 0.96f, 0.98f));
    drawText(L"请按提示完成正面、左侧、右侧各 2 次采集。", m_bodyFormat.Get(),
        D2D1::RectF(28, 58, 560, 84), Color(0.70f, 0.72f, 0.78f));

    D2D1_RECT_F previewRect = D2D1::RectF(28, 96, 592, 418);
    fillRound(previewRect, 16.0f, Color(0.12f, 0.13f, 0.16f));
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
            m_renderTarget->DrawBitmap(bitmap.Get(), previewRect, 0.92f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    } else {
        drawText(L"正在打开摄像头...", m_bodyFormat.Get(),
            D2D1::RectF(previewRect.left, previewRect.top + 135, previewRect.right, previewRect.top + 170),
            Color(0.58f, 0.60f, 0.66f));
    }
    drawRound(previewRect, 16.0f, Color(0.28f, 0.30f, 0.36f), 1.0f);

    D2D1_RECT_F statusRect = D2D1::RectF(28, 432, 592, 520);
    fillRound(statusRect, 12.0f, Color(0.10f, 0.11f, 0.14f));
    drawText(StepTitle(step), m_bodyFormat.Get(), D2D1::RectF(44, 444, 290, 468), Color(0.96f, 0.96f, 0.98f));
    drawText(status, m_captionFormat.Get(), D2D1::RectF(44, 468, 560, 490), Color(0.70f, 0.72f, 0.78f));

    wchar_t metrics[160] = {};
    swprintf_s(metrics, L"进度 %d/6    当前步骤 %d/2    Live %.2f    Yaw %.1f    Pitch %.1f",
        total, captured, live, yaw, pitch);
    drawText(metrics, m_captionFormat.Get(), D2D1::RectF(44, 493, 560, 512), Color(0.52f, 0.55f, 0.62f));

    float progress = static_cast<float>(total) / 6.0f;
    fillRound(D2D1::RectF(390, 450, 560, 456), 3.0f, Color(0.28f, 0.30f, 0.36f));
    fillRound(D2D1::RectF(390, 450, 390 + 170 * progress, 456), 3.0f, Color(0.30f, 0.58f, 1.0f));

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
                    SetStatus(L"未检测到人脸，请看向摄像头");
                    continue;
                }

                float real = liveness.Score(frame, *best);
                std::array<float, 62> params{};
                if (real < LivenessDetector::kDefaultRealThreshold || !landmarks.Run(frame, *best, params)) {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_liveScore = real;
                    }
                    SetStatus(L"等待真人检测通过");
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
                    m_status = L"已采集，保持姿势准备下一次";
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
            SetStatus(L"录入完成");
            m_result = Result::Completed;
        }
    } catch (const std::exception& ex) {
        std::wstring message = L"录入失败: ";
        std::string what = ex.what();
        message += std::wstring(what.begin(), what.end());
        SetStatus(message);
        m_result = Result::Failed;
        Sleep(1200);
    }

    m_done = true;
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_ENROLL_FINISHED, 0, 0);
    }
}
