#include "face_core/FaceRecognizer.h"

#include <array>
#include <cmath>

namespace face_core {

FaceRecognizer::FaceRecognizer() : session_(L"arcface_mbf.onnx", 2) {}

bool FaceRecognizer::Embed(const Image& aligned112, Embedding& out) {
    if (aligned112.width != 112 || aligned112.height != 112 || aligned112.channels != 3) {
        return false;
    }

    // ArcFace preprocessing (InsightFace convention):
    //   x = (BGR_uint8 - 127.5) / 127.5     -> [-1, 1]
    //   layout NCHW, BGR order preserved
    std::vector<float> chw(static_cast<size_t>(3) * 112 * 112);
    float* B = chw.data();
    float* G = chw.data() + static_cast<size_t>(112) * 112;
    float* R = chw.data() + static_cast<size_t>(2) * 112 * 112;
    for (int y = 0; y < 112; ++y) {
        const uint8_t* p = aligned112.ptr(y);
        for (int x = 0; x < 112; ++x) {
            B[y * 112 + x] = (static_cast<float>(p[x * 3 + 0]) - 127.5f) / 127.5f;
            G[y * 112 + x] = (static_cast<float>(p[x * 3 + 1]) - 127.5f) / 127.5f;
            R[y * 112 + x] = (static_cast<float>(p[x * 3 + 2]) - 127.5f) / 127.5f;
        }
    }

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> shape = {1, 3, 112, 112};
    Ort::Value input = Ort::Value::CreateTensor<float>(memInfo, chw.data(), chw.size(),
                                                       shape.data(), shape.size());

    auto outputs = session_.session().Run(
        Ort::RunOptions{nullptr},
        session_.InputNames().data(), &input, 1,
        session_.OutputNames().data(), session_.OutputNames().size());

    const float* feat = outputs[0].GetTensorData<float>();
    auto info = outputs[0].GetTensorTypeAndShapeInfo();
    if (info.GetElementCount() != kEmbeddingDim) return false;

    // L2 normalize.
    float norm = 0.0f;
    for (int i = 0; i < kEmbeddingDim; ++i) norm += feat[i] * feat[i];
    norm = std::sqrt(std::max(norm, 1e-12f));
    for (int i = 0; i < kEmbeddingDim; ++i) out[i] = feat[i] / norm;
    return true;
}

float FaceRecognizer::CosineSimilarity(const Embedding& a, const Embedding& b) {
    float dot = 0.0f;
    for (int i = 0; i < kEmbeddingDim; ++i) dot += a[i] * b[i];
    return dot;
}

}  // namespace face_core
