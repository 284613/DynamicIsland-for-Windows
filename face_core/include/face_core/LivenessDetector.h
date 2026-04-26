#pragma once

#include "face_core/FaceDetector.h"
#include "face_core/Image.h"
#include "face_core/ModelLoader.h"

namespace face_core {

class LivenessDetector {
public:
    LivenessDetector();

    // Returns Silent-Face class-1 probability: higher means likely real.
    float Score(const Image& bgr, const FaceBox& box);

    static constexpr float kDefaultRealThreshold = 0.70f;

private:
    OrtSession session_;
};

}  // namespace face_core
