#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "face_core/ActiveLiveness.h"
#include "face_core/FaceAligner.h"
#include "face_core/FaceDetector.h"
#include "face_core/FaceLandmarks3D.h"
#include "face_core/FaceRecognizer.h"
#include "face_core/FaceTemplateStore.h"
#include "face_core/LivenessDetector.h"

namespace face_core {

enum class PipelineStage : uint8_t {
    Idle,
    Detecting,
    Challenging,
    Verifying,
    Identifying,
    Success,
    Failed,
};

struct PipelineEvent {
    PipelineStage stage = PipelineStage::Idle;
    std::wstring hint;
    float score = 0.0f;
    std::string identity;
};

using PipelineCallback = std::function<void(const PipelineEvent&)>;

class FacePipeline {
public:
    struct Config {
        float matchThreshold = FaceRecognizer::kDefaultMatchThreshold;
        float realThreshold = LivenessDetector::kDefaultRealThreshold;
        int consecutiveMatches = 3;
        // Default off: enrollment captures multi-angle templates so daily
        // verification is "look at the camera" UX. Silent-Face still gates
        // photo/screen replay attacks. Flip on for high-security flows.
        bool requireActiveLiveness = false;
        bool requireSilentFace = true;
        uint32_t challengeSeed = 0;
    };

    FacePipeline(std::shared_ptr<FaceTemplateStore> store, const Config& cfg = Config{});

    bool OnFrame(const Image& bgr, double timestamp, PipelineCallback cb);
    void Reset();

private:
    void Emit(PipelineStage stage, const std::wstring& hint, float score,
              const std::string& identity, PipelineCallback& cb);
    uint32_t NextChallengeSeed() const;

    std::shared_ptr<FaceTemplateStore> store_;
    Config cfg_;
    FaceDetector detector_;
    LivenessDetector liveness_;
    FaceLandmarks3D landmarks_;
    FaceRecognizer recognizer_;
    ActiveLiveness active_;

    bool done_ = false;
    bool activeStarted_ = false;
    bool livePassed_ = false;
    bool haveStartTime_ = false;
    double startTime_ = 0.0;
    int consecutive_ = 0;
    std::string consecutiveName_;
};

}  // namespace face_core
