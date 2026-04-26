#include "face_core/LivenessDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace face_core {

namespace {

Image CropScaledFacePatch(const Image& src, const FaceBox& box, float scale) {
    if (src.empty() || src.channels != 3 || box.w <= 1.0f || box.h <= 1.0f) return {};

    const float srcW = static_cast<float>(src.width);
    const float srcH = static_cast<float>(src.height);
    scale = std::min({(srcH - 1.0f) / box.h, (srcW - 1.0f) / box.w, scale});

    const float newW = box.w * scale;
    const float newH = box.h * scale;
    const float centerX = box.x + box.w * 0.5f;
    const float centerY = box.y + box.h * 0.5f;
    float left = centerX - newW * 0.5f;
    float top = centerY - newH * 0.5f;
    float right = centerX + newW * 0.5f;
    float bottom = centerY + newH * 0.5f;

    if (left < 0.0f) {
        right -= left;
        left = 0.0f;
    }
    if (top < 0.0f) {
        bottom -= top;
        top = 0.0f;
    }
    if (right > srcW - 1.0f) {
        left -= right - srcW + 1.0f;
        right = srcW - 1.0f;
    }
    if (bottom > srcH - 1.0f) {
        top -= bottom - srcH + 1.0f;
        bottom = srcH - 1.0f;
    }

    const int x0 = std::clamp(static_cast<int>(left), 0, src.width - 1);
    const int y0 = std::clamp(static_cast<int>(top), 0, src.height - 1);
    const int x1 = std::clamp(static_cast<int>(right), x0, src.width - 1);
    const int y1 = std::clamp(static_cast<int>(bottom), y0, src.height - 1);
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;
    if (w <= 1 || h <= 1) return {};

    Image out(w, h, 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src.ptr(y0 + y) + x0 * 3;
        uint8_t* d = out.ptr(y);
        std::copy(s, s + static_cast<size_t>(w) * 3, d);
    }
    return out;
}

void BgrToChwFloat255(const Image& img, std::vector<float>& out) {
    const int w = img.width;
    const int h = img.height;
    out.resize(static_cast<size_t>(3) * w * h);
    float* b = out.data();
    float* g = b + static_cast<size_t>(w) * h;
    float* r = g + static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = img.ptr(y);
        for (int x = 0; x < w; ++x) {
            b[y * w + x] = static_cast<float>(row[x * 3 + 0]);
            g[y * w + x] = static_cast<float>(row[x * 3 + 1]);
            r[y * w + x] = static_cast<float>(row[x * 3 + 2]);
        }
    }
}

float SoftmaxClass1(const float* logits, size_t count) {
    if (count < 2) return 0.0f;
    float maxLogit = logits[0];
    for (size_t i = 1; i < count; ++i) maxLogit = std::max(maxLogit, logits[i]);
    float denom = 0.0f;
    float class1 = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float e = std::exp(logits[i] - maxLogit);
        if (i == 1) class1 = e;
        denom += e;
    }
    return denom > 0.0f ? class1 / denom : 0.0f;
}

}  // namespace

LivenessDetector::LivenessDetector() : session_(L"silent_face_anti_spoof.onnx", 2) {}

float LivenessDetector::Score(const Image& bgr, const FaceBox& box) {
    Image patch = CropScaledFacePatch(bgr, box, 2.7f);
    if (patch.empty()) return 0.0f;

    Image resized = ResizeBilinear(patch, 80, 80);
    std::vector<float> chw;
    BgrToChwFloat255(resized, chw);

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> shape = {1, 3, 80, 80};
    Ort::Value input = Ort::Value::CreateTensor<float>(memInfo, chw.data(), chw.size(),
                                                       shape.data(), shape.size());

    auto outputs = session_.session().Run(
        Ort::RunOptions{nullptr},
        session_.InputNames().data(), &input, 1,
        session_.OutputNames().data(), session_.OutputNames().size());

    const float* logits = outputs[0].GetTensorData<float>();
    size_t n = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    return SoftmaxClass1(logits, n);
}

}  // namespace face_core
