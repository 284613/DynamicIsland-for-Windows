#pragma once
#include <string>

namespace FaceCP {

// One-shot write to the main app's FaceUnlockBridge named pipe.
// Silently ignores errors (main app may not be running or behind the lock screen).
class IpcNotifier {
public:
    static void SendScanStarted();
    static void SendSuccess(const std::string& userName);
    static void SendFailed(const std::string& reason);
};

} // namespace FaceCP
