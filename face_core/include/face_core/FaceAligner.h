#pragma once
#include "face_core/FaceDetector.h"
#include "face_core/Image.h"

namespace face_core {

// ArcFace canonical 5-point template on a 112x112 crop. Order matches YuNet's
// keypoint order (right-eye, left-eye, nose, right-mouth, left-mouth from the
// subject's own perspective, i.e. mirrored on the viewer side).
extern const float kArcFaceRef112[10];

// Crop and align a face for ArcFace inference.
// Returns a 112x112 BGR image, or empty Image on failure.
Image AlignArcFace(const Image& bgr, const float src5[10]);

// 2D similarity transform (Umeyama, no reflection). Solves dst = s*R*src + t
// over N >= 2 point pairs and returns the *inverse* affine (dst -> src) as a
// 2x3 row-major matrix suitable for WarpAffineBilinear.
//   srcPts/dstPts: interleaved x,y arrays of length N*2.
// Returns false if the input is degenerate.
bool SimilarityTransformInverse(const float* srcPts, const float* dstPts, int n,
                                float outInverse[6]);

}  // namespace face_core
