#include "face_core/FaceAligner.h"

#include <cmath>

namespace face_core {

const float kArcFaceRef112[10] = {
    38.2946f, 51.6963f,
    73.5318f, 51.5014f,
    56.0252f, 71.7366f,
    41.5493f, 92.3655f,
    70.7299f, 92.2041f,
};

bool SimilarityTransformInverse(const float* srcPts, const float* dstPts, int n,
                                float outInverse[6]) {
    if (n < 2) return false;

    float meanSx = 0, meanSy = 0, meanDx = 0, meanDy = 0;
    for (int i = 0; i < n; ++i) {
        meanSx += srcPts[2 * i + 0];
        meanSy += srcPts[2 * i + 1];
        meanDx += dstPts[2 * i + 0];
        meanDy += dstPts[2 * i + 1];
    }
    meanSx /= n;
    meanSy /= n;
    meanDx /= n;
    meanDy /= n;

    float sxx = 0, sxy = 0;  // accumulates rotation
    float srcNorm = 0;
    for (int i = 0; i < n; ++i) {
        float sx = srcPts[2 * i + 0] - meanSx;
        float sy = srcPts[2 * i + 1] - meanSy;
        float dx = dstPts[2 * i + 0] - meanDx;
        float dy = dstPts[2 * i + 1] - meanDy;
        sxx += sx * dx + sy * dy;
        sxy += sx * dy - sy * dx;
        srcNorm += sx * sx + sy * sy;
    }
    if (srcNorm < 1e-6f) return false;

    float scale = std::sqrt(sxx * sxx + sxy * sxy) / srcNorm;
    if (scale < 1e-6f) return false;
    float c = sxx / (scale * srcNorm);  // cos(theta)
    float s = sxy / (scale * srcNorm);  // sin(theta)

    // Forward affine F: dst = scale * R * src + t.
    // t = meanD - scale*R*meanS.
    float tx = meanDx - scale * (c * meanSx - s * meanSy);
    float ty = meanDy - scale * (s * meanSx + c * meanSy);

    // Inverse F^-1: src = (1/scale) * R^T * (dst - t).
    float invS = 1.0f / scale;
    outInverse[0] = invS * c;
    outInverse[1] = invS * s;
    outInverse[2] = -invS * (c * tx + s * ty);
    outInverse[3] = -invS * s;
    outInverse[4] = invS * c;
    outInverse[5] = invS * (s * tx - c * ty);
    return true;
}

Image AlignArcFace(const Image& bgr, const float src5[10]) {
    float inv[6];
    if (!SimilarityTransformInverse(src5, kArcFaceRef112, 5, inv)) return {};
    return WarpAffineBilinear(bgr, inv, 112, 112);
}

}  // namespace face_core
