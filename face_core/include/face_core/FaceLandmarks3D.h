#pragma once

#include <array>

#include "face_core/FaceDetector.h"
#include "face_core/Image.h"
#include "face_core/ModelLoader.h"

namespace face_core {

struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

class FaceLandmarks3D {
public:
    FaceLandmarks3D();

    bool Run(const Image& bgr, const FaceBox& box, std::array<float, 62>& outParams);
    static HeadPose ExtractPose(const std::array<float, 62>& params);

private:
    OrtSession session_;
    std::array<float, 62> mean_{};
    std::array<float, 62> std_{};

    void LoadParamStats();
};

}  // namespace face_core
