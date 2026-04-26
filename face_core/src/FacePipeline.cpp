#include "face_core/FacePipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace face_core {

namespace {

bool TimedOut(double timestamp, double startTime) {
    return timestamp - startTime > ActiveLiveness::kTimeoutSeconds;
}

}  // namespace

FacePipeline::FacePipeline(std::shared_ptr<FaceTemplateStore> store, const Config& cfg)
    : store_(store ? std::move(store) : std::make_shared<FaceTemplateStore>()),
      cfg_(cfg) {}

bool FacePipeline::OnFrame(const Image& bgr, double timestamp, PipelineCallback cb) {
    if (done_) return false;
    if (!haveStartTime_) {
        startTime_ = timestamp;
        haveStartTime_ = true;
    }

    if (bgr.empty() || bgr.channels != 3) {
        Emit(PipelineStage::Detecting, L"No frame", 0.0f, {}, cb);
        return true;
    }

    std::vector<FaceBox> boxes = detector_.Detect(bgr);
    if (boxes.empty()) {
        if (TimedOut(timestamp, startTime_)) {
            Emit(PipelineStage::Failed, L"No face found", 0.0f, {}, cb);
            done_ = true;
            return false;
        }
        Emit(PipelineStage::Detecting, L"Looking for a face", 0.0f, {}, cb);
        return true;
    }

    const FaceBox& face = *std::max_element(
        boxes.begin(), boxes.end(),
        [](const FaceBox& a, const FaceBox& b) { return a.score < b.score; });

    if (cfg_.requireSilentFace && !livePassed_) {
        float real = liveness_.Score(bgr, face);
        if (real < cfg_.realThreshold) {
            if (TimedOut(timestamp, startTime_)) {
                Emit(PipelineStage::Failed, L"Passive liveness failed", real, {}, cb);
                done_ = true;
                return false;
            }
            Emit(PipelineStage::Verifying, L"Checking liveness", real, {}, cb);
            return true;
        }
        livePassed_ = true;
        Emit(PipelineStage::Verifying, L"Passive liveness passed", real, {}, cb);
    } else if (!cfg_.requireSilentFace) {
        livePassed_ = true;
    }

    if (cfg_.requireActiveLiveness) {
        std::array<float, 62> params{};
        if (!landmarks_.Run(bgr, face, params)) {
            if (TimedOut(timestamp, startTime_)) {
                Emit(PipelineStage::Failed, L"Pose could not be read", 0.0f, {}, cb);
                done_ = true;
                return false;
            }
            Emit(PipelineStage::Challenging, L"Reading head pose", 0.0f, {}, cb);
            return true;
        }

        HeadPose pose = FaceLandmarks3D::ExtractPose(params);
        if (!activeStarted_) {
            active_.Start(NextChallengeSeed());
            activeStarted_ = true;
        }

        ActiveState activeState = active_.OnFrame(pose, timestamp);
        if (activeState == ActiveState::FailTimeout || activeState == ActiveState::FailSpoof) {
            Emit(PipelineStage::Failed, active_.Hint(), 0.0f, {}, cb);
            done_ = true;
            return false;
        }
        if (activeState != ActiveState::Pass) {
            Emit(PipelineStage::Challenging, active_.Hint(), 0.0f, {}, cb);
            return true;
        }
    }

    Image aligned = AlignArcFace(bgr, face.kps);
    Embedding embedding{};
    if (aligned.empty() || !recognizer_.Embed(aligned, embedding)) {
        if (TimedOut(timestamp, startTime_)) {
            Emit(PipelineStage::Failed, L"Face alignment failed", 0.0f, {}, cb);
            done_ = true;
            return false;
        }
        Emit(PipelineStage::Identifying, L"Aligning face", 0.0f, {}, cb);
        return true;
    }

    MatchResult match = store_->Match(embedding, cfg_.matchThreshold);
    if (match.matched) {
        if (match.name == consecutiveName_) {
            ++consecutive_;
        } else {
            consecutiveName_ = match.name;
            consecutive_ = 1;
        }
    } else {
        consecutiveName_.clear();
        consecutive_ = 0;
    }

    if (match.matched && consecutive_ >= std::max(1, cfg_.consecutiveMatches)) {
        Emit(PipelineStage::Success, L"Face matched", match.score, match.name, cb);
        done_ = true;
        return false;
    }

    if (TimedOut(timestamp, startTime_)) {
        Emit(PipelineStage::Failed, L"No matching identity", match.score, match.name, cb);
        done_ = true;
        return false;
    }

    Emit(PipelineStage::Identifying, match.matched ? L"Confirming identity" : L"Looking for match",
         match.score, match.name, cb);
    return true;
}

void FacePipeline::Reset() {
    done_ = false;
    activeStarted_ = false;
    livePassed_ = false;
    haveStartTime_ = false;
    startTime_ = 0.0;
    consecutive_ = 0;
    consecutiveName_.clear();
    active_.Reset();
}

void FacePipeline::Emit(PipelineStage stage, const std::wstring& hint, float score,
                        const std::string& identity, PipelineCallback& cb) {
    if (!cb) return;
    cb(PipelineEvent{stage, hint, score, identity});
}

uint32_t FacePipeline::NextChallengeSeed() const {
    if (cfg_.challengeSeed != 0) return cfg_.challengeSeed;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<uint32_t>(now ^ (now >> 32));
}

}  // namespace face_core
