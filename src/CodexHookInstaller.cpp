#include "CodexHookInstaller.h"

#include "CodexHookBridge.h"
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Data::Json;

namespace {
std::filesystem::path GetUserProfilePath() {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &rawPath))) {
        return {};
    }

    std::filesystem::path path(rawPath);
    CoTaskMemFree(rawPath);
    return path;
}

std::wstring ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring text(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    return text;
}

bool WriteTextFile(const std::filesystem::path& path, const std::wstring& text) {
    std::filesystem::create_directories(path.parent_path());

    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return false;
    }

    std::string bytes(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), length, nullptr, nullptr);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::wstring DetectNodeCommand() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD result = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, buffer, nullptr);
    if (result == 0 || result >= MAX_PATH) {
        return {};
    }
    return L"\"" + std::wstring(buffer) + L"\"";
}

std::filesystem::path CodexDirectory() {
    return GetUserProfilePath() / L".codex";
}

std::filesystem::path HookConfigPath() {
    return CodexDirectory() / L"hooks.json";
}

std::filesystem::path HooksDirectory() {
    return CodexDirectory() / L"hooks";
}

std::filesystem::path ScriptPath() {
    return HooksDirectory() / L"dynamic-island-codex-state.js";
}

std::wstring HookScriptContent(const std::wstring& pipePath) {
    return
        L"#!/usr/bin/env node\n"
        L"const net = require('net');\n\n"
        L"const PIPE_PATH = String.raw`" + pipePath + L"`;\n\n"
        L"function readStdin() {\n"
        L"  return new Promise((resolve) => {\n"
        L"    let data = '';\n"
        L"    process.stdin.setEncoding('utf8');\n"
        L"    process.stdin.on('data', (chunk) => { data += chunk; });\n"
        L"    process.stdin.on('end', () => resolve(data));\n"
        L"    process.stdin.resume();\n"
        L"  });\n"
        L"}\n\n"
        L"function excerpt(value, maxLength = 140) {\n"
        L"  const text = String(value || '').replace(/\\s+/g, ' ').trim();\n"
        L"  if (!text) return '';\n"
        L"  return text.length <= maxLength ? text : text.slice(0, maxLength - 1) + '…';\n"
        L"}\n\n"
        L"function sendEvent(payload) {\n"
        L"  return new Promise((resolve) => {\n"
        L"    let settled = false;\n"
        L"    try {\n"
        L"      const client = net.createConnection(PIPE_PATH, () => {\n"
        L"        client.end(JSON.stringify(payload) + '\\n');\n"
        L"      });\n"
        L"      client.on('error', () => {\n"
        L"        if (!settled) {\n"
        L"          settled = true;\n"
        L"          resolve(false);\n"
        L"        }\n"
        L"      });\n"
        L"      client.on('close', () => {\n"
        L"        if (!settled) {\n"
        L"          settled = true;\n"
        L"          resolve(true);\n"
        L"        }\n"
        L"      });\n"
        L"    } catch {\n"
        L"      resolve(false);\n"
        L"    }\n"
        L"  });\n"
        L"}\n\n"
        L"function buildPayload(input) {\n"
        L"  const payload = {\n"
        L"    session_id: input.session_id || '',\n"
        L"    cwd: input.cwd || '',\n"
        L"    turn_id: input.turn_id || '',\n"
        L"    event: input.hook_event_name || '',\n"
        L"    tool: input.tool_name || '',\n"
        L"    tool_use_id: input.tool_use_id || '',\n"
        L"    permission_mode: input.permission_mode || '',\n"
        L"    source: input.source || '',\n"
        L"    message: '',\n"
        L"    tool_input_summary: ''\n"
        L"  };\n"
        L"  if (input.prompt) {\n"
        L"    payload.message = excerpt(input.prompt);\n"
        L"  } else if (input.last_assistant_message) {\n"
        L"    payload.message = excerpt(input.last_assistant_message);\n"
        L"  }\n"
        L"  if (input.tool_input && typeof input.tool_input.command === 'string') {\n"
        L"    payload.tool_input_summary = excerpt(input.tool_input.command);\n"
        L"  }\n"
        L"  return payload;\n"
        L"}\n\n"
        L"async function main() {\n"
        L"  let input = {};\n"
        L"  try {\n"
        L"    const raw = await readStdin();\n"
        L"    if (raw && raw.trim()) {\n"
        L"      input = JSON.parse(raw);\n"
        L"    }\n"
        L"  } catch {}\n"
        L"  try {\n"
        L"    await sendEvent(buildPayload(input));\n"
        L"  } catch {}\n"
        L"  process.stdout.write(JSON.stringify({ continue: true }));\n"
        L"}\n\n"
        L"main();\n";
}

bool CommandContainsDynamicIsland(const JsonObject& hook) {
    if (!hook.HasKey(L"command")) {
        return false;
    }

    try {
        return std::wstring(hook.GetNamedString(L"command").c_str()).find(L"dynamic-island-codex-state.js") != std::wstring::npos;
    } catch (...) {
        return false;
    }
}

JsonObject BuildCommandHook(const std::wstring& command) {
    JsonObject hook;
    hook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    hook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));
    return hook;
}

JsonObject BuildHookEntry(const std::wstring& command, const std::wstring& matcher, bool withMatcher) {
    JsonArray hooks;
    hooks.Append(BuildCommandHook(command));

    JsonObject entry;
    if (withMatcher && !matcher.empty()) {
        entry.SetNamedValue(L"matcher", JsonValue::CreateStringValue(matcher));
    }
    entry.SetNamedValue(L"hooks", hooks);
    return entry;
}

void UpsertHookArray(JsonObject& hooksObject, const std::wstring& eventName, const JsonArray& desiredEntries) {
    JsonArray merged;
    if (hooksObject.HasKey(eventName.c_str())) {
        try {
            const JsonArray existing = hooksObject.GetNamedArray(eventName.c_str());
            for (uint32_t i = 0; i < existing.Size(); ++i) {
                const JsonObject entry = existing.GetObjectAt(i);
                bool isOurEntry = false;
                if (entry.HasKey(L"hooks")) {
                    const JsonArray hookArray = entry.GetNamedArray(L"hooks");
                    for (uint32_t j = 0; j < hookArray.Size(); ++j) {
                        if (CommandContainsDynamicIsland(hookArray.GetObjectAt(j))) {
                            isOurEntry = true;
                            break;
                        }
                    }
                }
                if (!isOurEntry) {
                    merged.Append(entry);
                }
            }
        } catch (...) {
        }
    }

    for (uint32_t i = 0; i < desiredEntries.Size(); ++i) {
        merged.Append(desiredEntries.GetObjectAt(i));
    }
    hooksObject.SetNamedValue(eventName.c_str(), merged);
}

JsonObject LoadHookConfigRoot() {
    const std::wstring text = ReadTextFile(HookConfigPath());
    if (text.empty()) {
        return JsonObject();
    }

    try {
        return JsonObject::Parse(text);
    } catch (...) {
        return JsonObject();
    }
}
}

CodexHookInstallStatus CodexHookInstaller::DetectStatus() {
    CodexHookInstallStatus status;
    status.nodeCommand = DetectNodeCommand();
    status.nodeAvailable = !status.nodeCommand.empty();
    status.codexDirectoryExists = std::filesystem::exists(CodexDirectory());
    status.hooksDirectoryExists = std::filesystem::exists(HooksDirectory());
    status.scriptInstalled = std::filesystem::exists(ScriptPath());
    const std::wstring hookConfigText = ReadTextFile(HookConfigPath());
    status.hooksConfigured = hookConfigText.find(L"dynamic-island-codex-state.js") != std::wstring::npos;
    status.pipeListening = CodexHookBridge::IsListenerRunning();
    status.installed = status.nodeAvailable && status.scriptInstalled && status.hooksConfigured;
    return status;
}

CodexHookActionResult CodexHookInstaller::Install() {
    CodexHookActionResult result;
    const CodexHookInstallStatus status = DetectStatus();
    if (!status.nodeAvailable) {
        result.message = L"未检测到 Node.js";
        return result;
    }

    if (!WriteTextFile(ScriptPath(), HookScriptContent(CodexHookBridge::PipeName()))) {
        result.message = L"写入 Codex hook 脚本失败";
        return result;
    }

    JsonObject root = LoadHookConfigRoot();
    JsonObject hooksObject;
    if (root.HasKey(L"hooks")) {
        try {
            hooksObject = root.GetNamedObject(L"hooks");
        } catch (...) {
            hooksObject = JsonObject();
        }
    }

    const std::wstring command = status.nodeCommand + L" \"" + ScriptPath().wstring() + L"\"";
    JsonArray sessionStartArray;
    sessionStartArray.Append(BuildHookEntry(command, L"startup|resume|clear", true));
    JsonArray preToolUseArray;
    preToolUseArray.Append(BuildHookEntry(command, L"Bash", true));
    JsonArray noMatcherArray;
    noMatcherArray.Append(BuildHookEntry(command, L"", false));

    UpsertHookArray(hooksObject, L"SessionStart", sessionStartArray);
    UpsertHookArray(hooksObject, L"UserPromptSubmit", noMatcherArray);
    UpsertHookArray(hooksObject, L"PreToolUse", preToolUseArray);
    UpsertHookArray(hooksObject, L"PostToolUse", noMatcherArray);
    UpsertHookArray(hooksObject, L"Stop", noMatcherArray);

    root.SetNamedValue(L"hooks", hooksObject);

    if (!WriteTextFile(HookConfigPath(), root.Stringify().c_str())) {
        result.message = L"更新 hooks.json 失败";
        return result;
    }

    result.success = true;
    result.message = L"Codex hooks 已安装";
    return result;
}

CodexHookActionResult CodexHookInstaller::Reinstall() {
    Uninstall();
    return Install();
}

CodexHookActionResult CodexHookInstaller::Uninstall() {
    CodexHookActionResult result;
    std::error_code ec;
    std::filesystem::remove(ScriptPath(), ec);

    JsonObject root = LoadHookConfigRoot();
    if (root.HasKey(L"hooks")) {
        try {
            JsonObject hooksObject = root.GetNamedObject(L"hooks");
            const std::wstring events[] = {
                L"SessionStart", L"UserPromptSubmit", L"PreToolUse", L"PostToolUse", L"Stop"
            };

            for (const auto& eventName : events) {
                if (!hooksObject.HasKey(eventName.c_str())) {
                    continue;
                }

                JsonArray filtered;
                try {
                    const JsonArray existing = hooksObject.GetNamedArray(eventName.c_str());
                    for (uint32_t i = 0; i < existing.Size(); ++i) {
                        const JsonObject entry = existing.GetObjectAt(i);
                        bool isOurEntry = false;
                        if (entry.HasKey(L"hooks")) {
                            const JsonArray hookArray = entry.GetNamedArray(L"hooks");
                            for (uint32_t j = 0; j < hookArray.Size(); ++j) {
                                if (CommandContainsDynamicIsland(hookArray.GetObjectAt(j))) {
                                    isOurEntry = true;
                                    break;
                                }
                            }
                        }
                        if (!isOurEntry) {
                            filtered.Append(entry);
                        }
                    }
                } catch (...) {
                }

                if (filtered.Size() == 0) {
                    hooksObject.Remove(eventName.c_str());
                } else {
                    hooksObject.SetNamedValue(eventName.c_str(), filtered);
                }
            }

            if (hooksObject.Size() == 0) {
                root.Remove(L"hooks");
            } else {
                root.SetNamedValue(L"hooks", hooksObject);
            }
        } catch (...) {
        }
    }

    if (!WriteTextFile(HookConfigPath(), root.Stringify().c_str())) {
        result.message = L"移除 hooks.json 条目失败";
        return result;
    }

    result.success = true;
    result.message = L"Codex hooks 已卸载";
    return result;
}
