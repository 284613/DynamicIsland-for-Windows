#include "ClaudeHookInstaller.h"

#include "ClaudeHookBridge.h"
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
    int len = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), len);
    return text;
}

bool WriteTextFile(const std::filesystem::path& path, const std::wstring& text) {
    std::filesystem::create_directories(path.parent_path());
    int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), len, nullptr, nullptr);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::wstring DetectPythonCommand() {
    wchar_t buffer[MAX_PATH] = {};
    const wchar_t* candidates[] = { L"py.exe", L"python.exe", L"python3.exe" };
    for (const auto* candidate : candidates) {
        DWORD result = SearchPathW(nullptr, candidate, nullptr, MAX_PATH, buffer, nullptr);
        if (result > 0 && result < MAX_PATH) {
            if (std::wstring(candidate) == L"py.exe") {
                return L"py -3";
            }
            return L"\"" + std::wstring(buffer) + L"\"";
        }
    }
    return {};
}

std::filesystem::path ClaudeDirectory() {
    return GetUserProfilePath() / L".claude";
}

std::filesystem::path SettingsPath() {
    return ClaudeDirectory() / L"settings.json";
}

std::filesystem::path HooksDirectory() {
    return ClaudeDirectory() / L"hooks";
}

std::filesystem::path ScriptPath() {
    return HooksDirectory() / L"dynamic-island-state.py";
}

std::filesystem::path StatusLineScriptPath() {
    return HooksDirectory() / L"dynamic-island-statusline.py";
}

std::filesystem::path StatusLineSnapshotPath() {
    return ClaudeDirectory() / L"dynamic-island-statusline.json";
}

std::filesystem::path StatusLineBackupPath() {
    return HooksDirectory() / L"dynamic-island-statusline-command.txt";
}

std::wstring HookScriptContent(const std::wstring& pipePath) {
    return
        L"#!/usr/bin/env python3\n"
        L"import json\n"
        L"import os\n"
        L"import sys\n\n"
        L"PIPE_PATH = r\"" + pipePath + L"\"\n\n"
        L"def send_event(state, wait_for_response=False):\n"
        L"    try:\n"
        L"        with open(PIPE_PATH, 'r+b', buffering=0) as pipe:\n"
        L"            pipe.write((json.dumps(state) + '\\n').encode('utf-8'))\n"
        L"            pipe.flush()\n"
        L"            if wait_for_response:\n"
        L"                data = b''\n"
        L"                while not data.endswith(b'\\n'):\n"
        L"                    chunk = pipe.read(1)\n"
        L"                    if not chunk:\n"
        L"                        break\n"
        L"                    data += chunk\n"
        L"                if data:\n"
        L"                    return json.loads(data.decode('utf-8').strip())\n"
        L"    except OSError:\n"
        L"        return None\n"
        L"    return None\n\n"
        L"def main():\n"
        L"    try:\n"
        L"        data = json.load(sys.stdin)\n"
        L"    except Exception:\n"
        L"        sys.exit(1)\n\n"
        L"    event = data.get('hook_event_name', '')\n"
        L"    state = {\n"
        L"        'session_id': data.get('session_id', 'unknown'),\n"
        L"        'cwd': data.get('cwd', ''),\n"
        L"        'event': event,\n"
        L"        'pid': os.getppid(),\n"
        L"        'tty': None,\n"
        L"    }\n\n"
        L"    if event == 'UserPromptSubmit':\n"
        L"        state['status'] = 'processing'\n"
        L"    elif event == 'PreToolUse':\n"
        L"        state['status'] = 'running_tool'\n"
        L"        state['tool'] = data.get('tool_name')\n"
        L"        state['tool_input'] = data.get('tool_input', {})\n"
        L"        if data.get('tool_use_id'):\n"
        L"            state['tool_use_id'] = data.get('tool_use_id')\n"
        L"    elif event == 'PostToolUse':\n"
        L"        state['status'] = 'processing'\n"
        L"        state['tool'] = data.get('tool_name')\n"
        L"        state['tool_input'] = data.get('tool_input', {})\n"
        L"        if data.get('tool_use_id'):\n"
        L"            state['tool_use_id'] = data.get('tool_use_id')\n"
        L"    elif event == 'PermissionRequest':\n"
        L"        state['status'] = 'waiting_for_approval'\n"
        L"        state['tool'] = data.get('tool_name')\n"
        L"        state['tool_input'] = data.get('tool_input', {})\n"
        L"        response = send_event(state, True)\n"
        L"        if response:\n"
        L"            decision = response.get('decision', 'ask')\n"
        L"            reason = response.get('reason', '')\n"
        L"            if decision == 'allow':\n"
        L"                print(json.dumps({'hookSpecificOutput': {'hookEventName': 'PermissionRequest', 'decision': {'behavior': 'allow'}}}))\n"
        L"                sys.exit(0)\n"
        L"            if decision == 'deny':\n"
        L"                print(json.dumps({'hookSpecificOutput': {'hookEventName': 'PermissionRequest', 'decision': {'behavior': 'deny', 'message': reason or 'Denied by user via DynamicIsland'}}}))\n"
        L"                sys.exit(0)\n"
        L"        sys.exit(0)\n"
        L"    elif event == 'Notification':\n"
        L"        notification_type = data.get('notification_type')\n"
        L"        if notification_type == 'permission_prompt':\n"
        L"            sys.exit(0)\n"
        L"        if notification_type == 'idle_prompt':\n"
        L"            state['status'] = 'waiting_for_input'\n"
        L"        else:\n"
        L"            state['status'] = 'notification'\n"
        L"        state['notification_type'] = notification_type\n"
        L"        state['message'] = data.get('message')\n"
        L"    elif event == 'Stop':\n"
        L"        state['status'] = 'waiting_for_input'\n"
        L"    elif event == 'SubagentStop':\n"
        L"        state['status'] = 'waiting_for_input'\n"
        L"    elif event == 'SessionStart':\n"
        L"        state['status'] = 'waiting_for_input'\n"
        L"    elif event == 'SessionEnd':\n"
        L"        state['status'] = 'ended'\n"
        L"    elif event == 'PreCompact':\n"
        L"        state['status'] = 'compacting'\n"
        L"    else:\n"
        L"        state['status'] = 'unknown'\n\n"
        L"    send_event(state, False)\n\n"
        L"if __name__ == '__main__':\n"
        L"    main()\n";
}

std::wstring StatusLineMirrorScriptContent(const std::wstring& snapshotPath, const std::wstring& backupPath) {
    return
        L"#!/usr/bin/env python3\n"
        L"import json\n"
        L"import os\n"
        L"import subprocess\n"
        L"import sys\n\n"
        L"SNAPSHOT_PATH = r\"" + snapshotPath + L"\"\n"
        L"BACKUP_PATH = r\"" + backupPath + L"\"\n\n"
        L"def main():\n"
        L"    raw = sys.stdin.read()\n"
        L"    if raw:\n"
        L"        try:\n"
        L"            data = json.loads(raw)\n"
        L"            os.makedirs(os.path.dirname(SNAPSHOT_PATH), exist_ok=True)\n"
        L"            with open(SNAPSHOT_PATH, 'w', encoding='utf-8') as f:\n"
        L"                json.dump(data, f, ensure_ascii=False)\n"
        L"        except Exception:\n"
        L"            pass\n\n"
        L"    original_command = ''\n"
        L"    try:\n"
        L"        with open(BACKUP_PATH, 'r', encoding='utf-8') as f:\n"
        L"            original_command = f.read().strip()\n"
        L"    except OSError:\n"
        L"        original_command = ''\n\n"
        L"    if not original_command:\n"
        L"        return\n\n"
        L"    try:\n"
        L"        completed = subprocess.run(original_command, input=raw, text=True, shell=True, capture_output=True)\n"
        L"        if completed.stdout:\n"
        L"            sys.stdout.write(completed.stdout)\n"
        L"        if completed.stderr:\n"
        L"            sys.stderr.write(completed.stderr)\n"
        L"    except Exception:\n"
        L"        return\n\n"
        L"if __name__ == '__main__':\n"
        L"    main()\n";
}

bool CommandContainsDynamicIsland(const JsonObject& hook) {
    if (!hook.HasKey(L"command")) {
        return false;
    }
    try {
        return std::wstring(hook.GetNamedString(L"command").c_str()).find(L"dynamic-island-state.py") != std::wstring::npos;
    } catch (...) {
        return false;
    }
}

JsonObject BuildCommandHook(const std::wstring& command, bool includeTimeout) {
    JsonObject hook;
    hook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    hook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));
    if (includeTimeout) {
        hook.SetNamedValue(L"timeout", JsonValue::CreateNumberValue(300));
    }
    return hook;
}

JsonObject BuildHookEntry(const std::wstring& command, const std::wstring& matcher, bool includeTimeout, bool withMatcher) {
    JsonArray hooks;
    hooks.Append(BuildCommandHook(command, includeTimeout));
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
            JsonArray existing = hooksObject.GetNamedArray(eventName.c_str());
            for (uint32_t i = 0; i < existing.Size(); ++i) {
                JsonObject entry = existing.GetObjectAt(i);
                bool isOurEntry = false;
                if (entry.HasKey(L"hooks")) {
                    JsonArray hookArray = entry.GetNamedArray(L"hooks");
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

JsonObject LoadSettingsRoot() {
    const std::wstring text = ReadTextFile(SettingsPath());
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

ClaudeHookInstallStatus ClaudeHookInstaller::DetectStatus() {
    ClaudeHookInstallStatus status;
    status.pythonCommand = DetectPythonCommand();
    status.pythonAvailable = !status.pythonCommand.empty();
    status.hooksDirectoryExists = std::filesystem::exists(HooksDirectory());
    status.scriptInstalled = std::filesystem::exists(ScriptPath());
    status.statusLineMirrorInstalled = std::filesystem::exists(StatusLineScriptPath());
    const std::wstring settingsText = ReadTextFile(SettingsPath());
    status.settingsConfigured = settingsText.find(L"dynamic-island-state.py") != std::wstring::npos;
    status.statusLineConfigured = settingsText.find(L"dynamic-island-statusline.py") != std::wstring::npos;
    status.pipeListening = ClaudeHookBridge::IsListenerRunning();
    status.installed = status.pythonAvailable && status.scriptInstalled && status.settingsConfigured &&
        status.statusLineMirrorInstalled && status.statusLineConfigured;
    return status;
}

ClaudeHookActionResult ClaudeHookInstaller::Install() {
    ClaudeHookActionResult result;
    ClaudeHookInstallStatus status = DetectStatus();
    if (!status.pythonAvailable) {
        result.message = L"未检测到 Python";
        return result;
    }

    if (!WriteTextFile(ScriptPath(), HookScriptContent(ClaudeHookBridge::PipeName()))) {
        result.message = L"写入 hook 脚本失败";
        return result;
    }
    if (!WriteTextFile(StatusLineScriptPath(), StatusLineMirrorScriptContent(StatusLineSnapshotPath().wstring(), StatusLineBackupPath().wstring()))) {
        result.message = L"写入 statusLine 镜像脚本失败";
        return result;
    }

    JsonObject root = LoadSettingsRoot();
    JsonObject hooksObject;
    if (root.HasKey(L"hooks")) {
        try {
            hooksObject = root.GetNamedObject(L"hooks");
        } catch (...) {
            hooksObject = JsonObject();
        }
    }

    const std::wstring command = status.pythonCommand + L" \"" + ScriptPath().wstring() + L"\"";
    JsonArray defaultMatcherArray;
    defaultMatcherArray.Append(BuildHookEntry(command, L"*", false, true));
    JsonArray timeoutMatcherArray;
    timeoutMatcherArray.Append(BuildHookEntry(command, L"*", true, true));
    JsonArray noMatcherArray;
    noMatcherArray.Append(BuildHookEntry(command, L"", false, false));
    JsonArray preCompactArray;
    preCompactArray.Append(BuildHookEntry(command, L"auto", false, true));
    preCompactArray.Append(BuildHookEntry(command, L"manual", false, true));

    UpsertHookArray(hooksObject, L"UserPromptSubmit", noMatcherArray);
    UpsertHookArray(hooksObject, L"PreToolUse", defaultMatcherArray);
    UpsertHookArray(hooksObject, L"PostToolUse", defaultMatcherArray);
    UpsertHookArray(hooksObject, L"PermissionRequest", timeoutMatcherArray);
    UpsertHookArray(hooksObject, L"Notification", defaultMatcherArray);
    UpsertHookArray(hooksObject, L"Stop", noMatcherArray);
    UpsertHookArray(hooksObject, L"SubagentStop", noMatcherArray);
    UpsertHookArray(hooksObject, L"SessionStart", noMatcherArray);
    UpsertHookArray(hooksObject, L"SessionEnd", noMatcherArray);
    UpsertHookArray(hooksObject, L"PreCompact", preCompactArray);

    root.SetNamedValue(L"hooks", hooksObject);

    std::wstring existingStatusLineCommand;
    if (root.HasKey(L"statusLine")) {
        try {
            JsonObject statusLine = root.GetNamedObject(L"statusLine");
            existingStatusLineCommand = statusLine.GetNamedString(L"command").c_str();
        } catch (...) {
        }
    }
    if (!existingStatusLineCommand.empty() &&
        existingStatusLineCommand.find(L"dynamic-island-statusline.py") == std::wstring::npos) {
        WriteTextFile(StatusLineBackupPath(), existingStatusLineCommand);
    }

    JsonObject statusLineConfig;
    statusLineConfig.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    statusLineConfig.SetNamedValue(L"command", JsonValue::CreateStringValue(status.pythonCommand + L" \"" + StatusLineScriptPath().wstring() + L"\""));
    root.SetNamedValue(L"statusLine", statusLineConfig);

    if (!WriteTextFile(SettingsPath(), root.Stringify().c_str())) {
        result.message = L"更新 settings.json 失败";
        return result;
    }

    result.success = true;
    result.message = L"Claude hooks 已安装";
    return result;
}

ClaudeHookActionResult ClaudeHookInstaller::Reinstall() {
    Uninstall();
    return Install();
}

ClaudeHookActionResult ClaudeHookInstaller::Uninstall() {
    ClaudeHookActionResult result;
    std::error_code ec;
    std::filesystem::remove(ScriptPath(), ec);
    std::filesystem::remove(StatusLineScriptPath(), ec);
    std::filesystem::remove(StatusLineSnapshotPath(), ec);

    JsonObject root = LoadSettingsRoot();
    if (root.HasKey(L"hooks")) {
        try {
            JsonObject hooksObject = root.GetNamedObject(L"hooks");
            const std::wstring events[] = {
                L"UserPromptSubmit", L"PreToolUse", L"PostToolUse", L"PermissionRequest",
                L"Notification", L"Stop", L"SubagentStop", L"SessionStart", L"SessionEnd", L"PreCompact"
            };

            for (const auto& eventName : events) {
                if (!hooksObject.HasKey(eventName.c_str())) {
                    continue;
                }

                JsonArray filtered;
                try {
                    JsonArray existing = hooksObject.GetNamedArray(eventName.c_str());
                    for (uint32_t i = 0; i < existing.Size(); ++i) {
                        JsonObject entry = existing.GetObjectAt(i);
                        bool isOurEntry = false;
                        if (entry.HasKey(L"hooks")) {
                            JsonArray hookArray = entry.GetNamedArray(L"hooks");
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

    if (root.HasKey(L"statusLine")) {
        try {
            JsonObject statusLine = root.GetNamedObject(L"statusLine");
            const std::wstring command = statusLine.GetNamedString(L"command").c_str();
            if (command.find(L"dynamic-island-statusline.py") != std::wstring::npos) {
                const std::wstring previousCommand = ReadTextFile(StatusLineBackupPath());
                if (!previousCommand.empty()) {
                    JsonObject restore;
                    restore.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
                    restore.SetNamedValue(L"command", JsonValue::CreateStringValue(previousCommand));
                    root.SetNamedValue(L"statusLine", restore);
                } else {
                    root.Remove(L"statusLine");
                }
            }
        } catch (...) {
        }
    }

    if (!WriteTextFile(SettingsPath(), root.Stringify().c_str())) {
        result.message = L"移除 settings hook 失败";
        return result;
    }
    std::filesystem::remove(StatusLineBackupPath(), ec);

    result.success = true;
    result.message = L"Claude hooks 已卸载";
    return result;
}
