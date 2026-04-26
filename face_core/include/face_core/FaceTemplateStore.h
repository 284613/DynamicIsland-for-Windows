#pragma once
#include <optional>
#include <string>
#include <vector>

#include "face_core/FaceRecognizer.h"

namespace face_core {

struct FaceTemplate {
    std::string name;       // up to 32 ASCII chars; identifies the user
    uint8_t angleTag = 0;   // 0=front, 1=left, 2=right (enrollment angle)
    Embedding embedding{};
};

struct MatchResult {
    bool matched = false;
    std::string name;
    float score = 0.0f;     // best cosine similarity across all templates
};

// Persistent, DPAPI-encrypted ArcFace template store. Backed by
// %LOCALAPPDATA%\DynamicIsland\faces.bin. Multiple templates per user are
// supported (typically 6: front/left/right x 2 captures each).
class FaceTemplateStore {
public:
    FaceTemplateStore();

    // Load existing store; returns true even if file is missing (empty store).
    bool Load();
    // Persist current state. Throws std::runtime_error on disk/CRYPT failure.
    void Save();

    void Add(const FaceTemplate& tpl);
    bool Remove(const std::string& name);  // removes ALL templates for the user
    std::vector<std::string> ListNames() const;
    size_t Count() const { return templates_.size(); }
    size_t CountForName(const std::string& name) const;
    void Clear();

    // Find the best match across all stored templates.
    MatchResult Match(const Embedding& probe,
                      float threshold = FaceRecognizer::kDefaultMatchThreshold) const;

    // Resolved on-disk path (created lazily inside Save()).
    std::wstring Path() const { return path_; }

private:
    std::wstring path_;
    std::vector<FaceTemplate> templates_;
};

}  // namespace face_core
