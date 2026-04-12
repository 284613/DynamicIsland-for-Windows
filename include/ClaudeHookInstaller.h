#pragma once

#include <string>

struct ClaudeHookInstallStatus {
    bool pythonAvailable = false;
    std::wstring pythonCommand;
    bool hooksDirectoryExists = false;
    bool scriptInstalled = false;
    bool settingsConfigured = false;
    bool statusLineConfigured = false;
    bool statusLineMirrorInstalled = false;
    bool installed = false;
    bool pipeListening = false;
};

struct ClaudeHookActionResult {
    bool success = false;
    std::wstring message;
};

class ClaudeHookInstaller {
public:
    static ClaudeHookInstallStatus DetectStatus();
    static ClaudeHookActionResult Install();
    static ClaudeHookActionResult Reinstall();
    static ClaudeHookActionResult Uninstall();
};
