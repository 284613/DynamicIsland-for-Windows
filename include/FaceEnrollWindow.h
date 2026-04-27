#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

class FaceEnrollWindow {
public:
    enum class Result {
        Cancelled,
        Completed,
        Failed,
    };

    FaceEnrollWindow();
    ~FaceEnrollWindow();

    Result ShowModal(HINSTANCE instance, HWND owner);
    const std::wstring& GetErrorMessage() const { return m_errorMessage; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateWindowResources();
    bool EnsureRenderTarget();
    void DiscardRenderTarget();
    void Render();
    void StartWorker();
    void StopWorker();
    void WorkerMain();
    void SetStatus(const std::wstring& status);
    void SetPreviewFrame(int width, int height, const std::vector<unsigned char>& bgra);
    std::string EnrollmentName() const;

    HWND m_hwnd = nullptr;
    HWND m_owner = nullptr;
    HINSTANCE m_instance = nullptr;
    Result m_result = Result::Cancelled;
    std::atomic<bool> m_cancel{ false };
    std::atomic<bool> m_done{ false };
    std::thread m_worker;

    mutable std::mutex m_mutex;
    std::wstring m_status = L"正在初始化摄像头...";
    std::wstring m_errorMessage;
    int m_step = 0;
    int m_capturedInStep = 0;
    int m_totalCaptured = 0;
    float m_liveScore = 0.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    int m_previewWidth = 0;
    int m_previewHeight = 0;
    std::vector<unsigned char> m_previewBgra;

    Microsoft::WRL::ComPtr<ID2D1Factory1> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_bodyFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_captionFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;
};
