#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>
#include <credentialprovider.h>

namespace FaceCP {

// Drives face_core::FacePipeline on a background thread.
// Updates CP status text via ICredentialProviderCredentialEvents.
// Calls onSuccess() when recognition succeeds so the provider can
// fire ICredentialProviderEvents::CredentialsChanged for auto-submit.
class UnlockOrchestrator {
public:
    UnlockOrchestrator() = default;
    ~UnlockOrchestrator();

    // Start recognition. Holds AddRef on both pointers until Stop().
    // statusFieldId: field index for hint text updates.
    // onSuccess: called on the worker thread after a successful match.
    void Start(ICredentialProviderCredentialEvents* events,
               ICredentialProviderCredential* credential,
               DWORD statusFieldId,
               std::function<void()> onSuccess);
    void Stop();

    bool HasSucceeded() const { return m_succeeded.load(); }
    bool IsRunning() const { return m_running.load(); }
    void ResetSucceeded();
    std::wstring MatchedUser() const;

private:
    void WorkerLoop();

    ICredentialProviderCredentialEvents* m_events     = nullptr;
    ICredentialProviderCredential*       m_credential = nullptr;
    DWORD                                m_statusFieldId = 0;
    std::function<void()>                m_onSuccess;

    std::thread       m_worker;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_succeeded{false};
    std::atomic<bool> m_running{false};

    mutable std::mutex m_mutex;
    std::wstring       m_matchedUser;

    // Disallow copy/move
    UnlockOrchestrator(const UnlockOrchestrator&) = delete;
    UnlockOrchestrator& operator=(const UnlockOrchestrator&) = delete;
};

} // namespace FaceCP
