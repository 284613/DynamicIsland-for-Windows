#include "face_core/ActiveLiveness.h"

#include <cmath>
#include <random>

namespace face_core {

void ActiveLiveness::Start(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 1);
    challenge_ = dist(rng) == 0 ? Challenge::TurnLeft : Challenge::TurnRight;
    state_ = ActiveState::Detect;
    haveInitialPose_ = false;
    crossedThreshold_ = false;
    initialYaw_ = 0.0f;
    startTime_ = 0.0;
}

void ActiveLiveness::Reset() {
    state_ = ActiveState::Idle;
    haveInitialPose_ = false;
    crossedThreshold_ = false;
    initialYaw_ = 0.0f;
    startTime_ = 0.0;
}

ActiveState ActiveLiveness::OnFrame(const HeadPose& pose, double timestampSeconds) {
    if (state_ == ActiveState::Idle || state_ == ActiveState::Pass ||
        state_ == ActiveState::FailTimeout || state_ == ActiveState::FailSpoof) {
        return state_;
    }

    if (!haveInitialPose_) {
        initialYaw_ = pose.yaw;
        startTime_ = timestampSeconds;
        haveInitialPose_ = true;
        state_ = ActiveState::Challenge;
        return state_;
    }

    if (timestampSeconds - startTime_ > kTimeoutSeconds) {
        state_ = ActiveState::FailTimeout;
        return state_;
    }

    const float dyaw = pose.yaw - initialYaw_;
    const bool thresholdMet =
        challenge_ == Challenge::TurnLeft ? dyaw <= -kYawThresholdDeg : dyaw >= kYawThresholdDeg;
    if (thresholdMet) {
        crossedThreshold_ = true;
        state_ = ActiveState::Verify;
    }

    if (crossedThreshold_ && std::fabs(dyaw) < 4.0f) {
        state_ = ActiveState::Pass;
    }
    return state_;
}

const wchar_t* ActiveLiveness::Hint() const {
    switch (state_) {
    case ActiveState::Idle:
    case ActiveState::Detect:
        return L"Look at the camera";
    case ActiveState::Challenge:
        return challenge_ == Challenge::TurnLeft ? L"Turn your head left" : L"Turn your head right";
    case ActiveState::Verify:
        return L"Return to center";
    case ActiveState::Pass:
        return L"Liveness passed";
    case ActiveState::FailTimeout:
        return L"Liveness timed out";
    case ActiveState::FailSpoof:
        return L"Liveness failed";
    }
    return L"";
}

}  // namespace face_core
