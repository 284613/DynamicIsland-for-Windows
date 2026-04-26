#pragma once
#include <memory>
#include <string>

#include <onnxruntime_cxx_api.h>

namespace face_core {

// Process-wide ORT environment. First call constructs; subsequent calls share.
Ort::Env& SharedEnv();

// Resolve a model file by short name. Search order:
//   1. <exe-dir>\models\<name>
//   2. %LOCALAPPDATA%\DynamicIsland\models\<name>
//   3. <repo-dir>\models\<name>   (dev fallback, walks up from exe)
// Returns empty string if missing — caller decides how to fail.
std::wstring ResolveModelPath(const std::wstring& name);

// Wrap an Ort::Session built from one of the four pipeline ONNX files.
// Construction throws std::runtime_error on missing file or load failure.
class OrtSession {
public:
    OrtSession(const std::wstring& modelName, int intraOpThreads = 2);

    Ort::Session& session() { return session_; }
    Ort::AllocatorWithDefaultOptions& allocator() { return allocator_; }

    // Cached input/output names (UTF-8). Pointers stay valid for the session lifetime.
    const std::vector<const char*>& InputNames() const { return inputCStr_; }
    const std::vector<const char*>& OutputNames() const { return outputCStr_; }

private:
    Ort::SessionOptions options_;
    Ort::Session session_;
    Ort::AllocatorWithDefaultOptions allocator_;
    std::vector<Ort::AllocatedStringPtr> inputOwned_;
    std::vector<Ort::AllocatedStringPtr> outputOwned_;
    std::vector<const char*> inputCStr_;
    std::vector<const char*> outputCStr_;
};

}  // namespace face_core
