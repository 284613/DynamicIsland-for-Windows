#include "face_core/ModelLoader.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace face_core {

Ort::Env& SharedEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "face_core");
    return env;
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return L"";
    fs::path p(buf);
    return p.parent_path().wstring();
}

static std::wstring LocalAppDataModelsDir() {
    wchar_t* roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &roaming))) return L"";
    fs::path p(roaming);
    CoTaskMemFree(roaming);
    p /= L"DynamicIsland";
    p /= L"models";
    return p.wstring();
}

static std::wstring ProgramDataModelsDir() {
    wchar_t* pd = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) return L"";
    fs::path p(pd);
    CoTaskMemFree(pd);
    p /= L"DynamicIsland";
    p /= L"models";
    return p.wstring();
}

std::wstring ResolveModelPath(const std::wstring& name) {
    std::vector<fs::path> candidates;
    fs::path exe = ExeDir();
    if (!exe.empty()) {
        candidates.push_back(exe / L"models" / name);
        // Dev fallback: when running from x64\Debug, walk up to repo root.
        for (int i = 0; i < 4; ++i) {
            exe = exe.parent_path();
            if (exe.empty()) break;
            candidates.push_back(exe / L"models" / name);
        }
    }
    fs::path appData = LocalAppDataModelsDir();
    if (!appData.empty()) {
        candidates.push_back(appData / name);
    }
    fs::path progData = ProgramDataModelsDir();
    if (!progData.empty()) {
        candidates.push_back(progData / name);
    }
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) return c.wstring();
    }
    return L"";
}

OrtSession::OrtSession(const std::wstring& modelName, int intraOpThreads)
    : session_(nullptr) {
    std::wstring path = ResolveModelPath(modelName);
    if (path.empty()) {
        std::string narrow;
        narrow.reserve(modelName.size());
        for (wchar_t c : modelName) {
            narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
        }
        throw std::runtime_error("face_core: model not found: " + narrow);
    }
    options_.SetIntraOpNumThreads(intraOpThreads);
    options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_ = Ort::Session(SharedEnv(), path.c_str(), options_);

    size_t nIn = session_.GetInputCount();
    size_t nOut = session_.GetOutputCount();
    inputOwned_.reserve(nIn);
    outputOwned_.reserve(nOut);
    inputCStr_.reserve(nIn);
    outputCStr_.reserve(nOut);
    for (size_t i = 0; i < nIn; ++i) {
        inputOwned_.emplace_back(session_.GetInputNameAllocated(i, allocator_));
        inputCStr_.push_back(inputOwned_.back().get());
    }
    for (size_t i = 0; i < nOut; ++i) {
        outputOwned_.emplace_back(session_.GetOutputNameAllocated(i, allocator_));
        outputCStr_.push_back(outputOwned_.back().get());
    }
}

}  // namespace face_core
