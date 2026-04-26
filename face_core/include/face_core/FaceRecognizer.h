#pragma once
#include <array>
#include <vector>

#include "face_core/Image.h"
#include "face_core/ModelLoader.h"

namespace face_core {

constexpr int kEmbeddingDim = 512;
using Embedding = std::array<float, kEmbeddingDim>;

class FaceRecognizer {
public:
    FaceRecognizer();

    // Compute a unit-normalized embedding from a 112x112 BGR aligned face.
    // Returns false if the image is the wrong shape.
    bool Embed(const Image& aligned112, Embedding& out);

    // Cosine similarity (range [-1, 1]). Both embeddings are expected unit-norm.
    static float CosineSimilarity(const Embedding& a, const Embedding& b);

    // Default match threshold for InsightFace MobileFaceNet at 112x112.
    // Same-identity pairs typically score > 0.42; tune with field data.
    static constexpr float kDefaultMatchThreshold = 0.42f;

private:
    OrtSession session_;
};

}  // namespace face_core
