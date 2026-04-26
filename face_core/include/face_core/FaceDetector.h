#pragma once
#include <memory>
#include <vector>

#include "face_core/Image.h"
#include "face_core/ModelLoader.h"

namespace face_core {

struct FaceBox {
    float x = 0, y = 0, w = 0, h = 0;
    float score = 0;
    // 5 landmarks order: right-eye, left-eye, nose-tip, right-mouth, left-mouth
    // (mirrored relative to viewer because YuNet labels by subject's perspective).
    float kps[10] = {0};
};

class FaceDetector {
public:
    explicit FaceDetector(float confThreshold = 0.6f, float nmsIoU = 0.4f);

    // Run detection on a BGR image of any size. Returns boxes in source-image coords.
    std::vector<FaceBox> Detect(const Image& bgr);

    void SetConfThreshold(float t) { confThreshold_ = t; }
    void SetNmsIoU(float t) { nmsIoU_ = t; }

private:
    static constexpr int kInputSize = 640;
    OrtSession session_;
    float confThreshold_;
    float nmsIoU_;
};

}  // namespace face_core
