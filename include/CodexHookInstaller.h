#pragma once

#include <string>

struct CodexHookInstallStatus {
    bool nodeAvailable = false;
    std::wstring nodeCommand;
    bool codexDirectoryExists = false;
    bool hooksDirectoryExists = false;
    bool scriptInstalled = false;
    bool hooksConfigured = false;
    bool installed = false;
    bool pipeListening = false;
};

struct CodexHookActionResult {
    bool success = false;
    std::wstring message;
};

class CodexHookInstaller {
public:
    static CodexHookInstallStatus DetectStatus();
    static CodexHookActionResult Install();
    static CodexHookActionResult Reinstall();
    static CodexHookActionResult Uninstall();
};
