#pragma once

#include <cstdint>

#include "face_core/FaceLandmarks3D.h"

namespace face_core {

enum class Challenge : uint8_t {
    TurnLeft = 0,
    TurnRight = 1,
};

enum class ActiveState : uint8_t {
    Idle,
    Detect,
    Challenge,
    Verify,
    Pass,
    FailTimeout,
    FailSpoof,
};

class ActiveLiveness {
public:
    void Start(uint32_t seed);
    void Reset();

    Challenge CurrentChallenge() const { return challenge_; }
    ActiveState State() const { return state_; }

    ActiveState OnFrame(const HeadPose& pose, double timestampSeconds);
    const wchar_t* Hint() const;

    static constexpr float kYawThresholdDeg = 12.0f;
    static constexpr double kTimeoutSeconds = 5.0;

private:
    Challenge challenge_ = Challenge::TurnLeft;
    ActiveState state_ = ActiveState::Idle;
    bool haveInitialPose_ = false;
    bool crossedThreshold_ = false;
    float initialYaw_ = 0.0f;
    double startTime_ = 0.0;
};

}  // namespace face_core
