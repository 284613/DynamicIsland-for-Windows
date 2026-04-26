#pragma once

// Shared static library for camera, ONNX inference, and face unlock pipeline.

#include "face_core/ActiveLiveness.h"
#include "face_core/CameraCapture.h"
#include "face_core/FaceAligner.h"
#include "face_core/FaceDetector.h"
#include "face_core/FaceLandmarks3D.h"
#include "face_core/FacePipeline.h"
#include "face_core/FaceRecognizer.h"
#include "face_core/FaceTemplateStore.h"
#include "face_core/Image.h"
#include "face_core/LivenessDetector.h"
#include "face_core/ModelLoader.h"

namespace face_core {

constexpr const char* kVersion = "0.1.0-face-core-phase1";

}  // namespace face_core
