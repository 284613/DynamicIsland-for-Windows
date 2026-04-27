#include "UnlockOrchestrator.h"
#include "CpLogger.h"
#include "IpcNotifier.h"

#include "face_core/CameraCapture.h"
#include "face_core/FacePipeline.h"
#include "face_core/FaceTemplateStore.h"

#include <chrono>
#include <memory>

using namespace face_core;

namespace FaceCP {

UnlockOrchestrator::~UnlockOrchestrator() {
    Stop();
}

void UnlockOrchestrator::Start(ICredentialProviderCredentialEvents* events,
                                ICredentialProviderCredential* credential,
                                DWORD statusFieldId,
                                std::function<void()> onSuccess)
{
    CpLog(L"UnlockOrchestrator::Start");
    Stop();
    m_events        = events;
    m_credential    = credential;
    m_statusFieldId = statusFieldId;
    m_onSuccess     = std::move(onSuccess);
    m_stop          = false;
    m_succeeded     = false;
    m_running       = true;
    if (m_events)     m_events->AddRef();
    if (m_credential) m_credential->AddRef();
    m_worker = std::thread([this]() { WorkerLoop(); });
}

void UnlockOrchestrator::Stop() {
    CpLog(L"UnlockOrchestrator::Stop joinable=%d", m_worker.joinable());
    m_stop = true;
    if (m_worker.joinable()) m_worker.join();
    m_running = false;
    if (m_events)     { m_events->Release();     m_events     = nullptr; }
    if (m_credential) { m_credential->Release(); m_credential = nullptr; }
    m_onSuccess = nullptr;
}

void UnlockOrchestrator::ResetSucceeded() {
    CpLog(L"UnlockOrchestrator::ResetSucceeded");
    m_succeeded = false;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_matchedUser.clear();
}

std::wstring UnlockOrchestrator::MatchedUser() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_matchedUser;
}

void UnlockOrchestrator::WorkerLoop() {
    CpLog(L"UnlockOrchestrator::WorkerLoop begin");
    struct RunningReset {
        std::atomic<bool>& running;
        ~RunningReset() { running = false; }
    } runningReset{m_running};

    IpcNotifier::SendScanStarted();

    // Snapshot interface pointers — Stop() may run concurrently after m_stop is set.
    auto* events     = m_events;
    auto* credential = m_credential;
    DWORD fieldId    = m_statusFieldId;

    auto setStatus = [&](const wchar_t* text) {
        if (events && credential && !m_stop)
            events->SetFieldString(credential, fieldId, text);
    };

    setStatus(L"正在打开摄像头...");

    auto store = std::make_shared<FaceTemplateStore>(FaceTemplateStore::SharedPath());
    if (!store->Load() || store->Count() == 0) {
        CpLog(L"UnlockOrchestrator no templates");
        setStatus(L"未找到人脸模板，请输入密码");
        IpcNotifier::SendFailed("no_templates");
        return;
    }

    FacePipeline::Config cfg;
    cfg.requireActiveLiveness = false;
    FacePipeline pipeline(store, cfg);

    CameraCapture cam;
    if (!cam.Open()) {
        CpLog(L"UnlockOrchestrator camera open failed");
        setStatus(L"无法打开摄像头，请输入密码");
        IpcNotifier::SendFailed("no_camera");
        return;
    }

    setStatus(L"扫描中，请面向摄像头...");

    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    constexpr double kMaxSeconds = 30.0;

    bool faceSucceeded = false;
    std::string matchedIdentity;

    while (!m_stop) {
        double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
        if (elapsed > kMaxSeconds) {
            CpLog(L"UnlockOrchestrator timeout");
            setStatus(L"识别超时，请输入密码");
            IpcNotifier::SendFailed("timeout");
            break;
        }

        Image frame = cam.ReadFrame();
        if (frame.data.empty()) {
            Sleep(30);
            continue;
        }

        bool running = pipeline.OnFrame(frame, elapsed, [&](const PipelineEvent& ev) {
            switch (ev.stage) {
            case PipelineStage::Detecting:
                setStatus(L"检测中...");
                break;
            case PipelineStage::Verifying:
                setStatus(L"活体检测...");
                break;
            case PipelineStage::Identifying:
                setStatus(L"识别中...");
                break;
            case PipelineStage::Challenging:
                setStatus(ev.hint.empty() ? L"请按提示动作" : ev.hint.c_str());
                break;
            case PipelineStage::Success:
                faceSucceeded   = true;
                matchedIdentity = ev.identity;
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_matchedUser = std::wstring(ev.identity.begin(), ev.identity.end());
                }
                setStatus(L"识别成功！");
                break;
            case PipelineStage::Failed:
                setStatus(L"识别失败，请输入密码");
                break;
            default:
                break;
            }
        });

        if (!running) break;
    }

    if (faceSucceeded && !m_stop) {
        CpLog(L"UnlockOrchestrator success identity='%S'", matchedIdentity.c_str());
        m_succeeded = true;
        IpcNotifier::SendSuccess(matchedIdentity);
        // Notify the provider to call ICredentialProviderEvents::CredentialsChanged.
        if (m_onSuccess) m_onSuccess();
    } else if (!m_stop && !faceSucceeded) {
        CpLog(L"UnlockOrchestrator failed no_match");
        IpcNotifier::SendFailed("no_match");
    }
}

} // namespace FaceCP
