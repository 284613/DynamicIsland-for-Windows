#include "face_core/FaceDetector.h"

#include <algorithm>
#include <cmath>

namespace face_core {

namespace {

// YuNet 2023mar ships with Sigmoid baked into the cls/obj heads, so the raw
// outputs are already in [0,1]. Do NOT apply sigmoid here — that produced
// the classic "everything scores 0.5" symptom (no-face: 0.5*0.5=0.25,
// real-face: sigmoid(~1)*sigmoid(~1)=~0.51).

// Standard IoU NMS over a vector of indices sorted by score (desc).
std::vector<int> NMS(const std::vector<FaceBox>& boxes, float iouThr) {
    std::vector<int> idx(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) idx[i] = static_cast<int>(i);
    std::sort(idx.begin(), idx.end(),
              [&](int a, int b) { return boxes[a].score > boxes[b].score; });
    std::vector<int> keep;
    std::vector<bool> suppressed(boxes.size(), false);
    for (int i : idx) {
        if (suppressed[i]) continue;
        keep.push_back(i);
        const auto& a = boxes[i];
        float ax2 = a.x + a.w, ay2 = a.y + a.h;
        for (int j : idx) {
            if (j == i || suppressed[j]) continue;
            const auto& b = boxes[j];
            float bx2 = b.x + b.w, by2 = b.y + b.h;
            float xx1 = std::max(a.x, b.x);
            float yy1 = std::max(a.y, b.y);
            float xx2 = std::min(ax2, bx2);
            float yy2 = std::min(ay2, by2);
            float inter = std::max(0.0f, xx2 - xx1) * std::max(0.0f, yy2 - yy1);
            float uni = a.w * a.h + b.w * b.h - inter;
            if (uni > 0 && inter / uni > iouThr) suppressed[j] = true;
        }
    }
    return keep;
}

// HWC uint8 BGR (or RGB; YuNet was trained on BGR mean=0 stride=1) -> CHW float32.
// Input: src is exactly kInputSize x kInputSize, 3 channels.
void HWCtoCHW(const Image& src, std::vector<float>& dst) {
    const int W = src.width, H = src.height;
    dst.resize(static_cast<size_t>(3) * H * W);
    float* B = dst.data();
    float* G = dst.data() + static_cast<size_t>(H) * W;
    float* R = dst.data() + static_cast<size_t>(2) * H * W;
    for (int y = 0; y < H; ++y) {
        const uint8_t* p = src.ptr(y);
        for (int x = 0; x < W; ++x) {
            B[y * W + x] = static_cast<float>(p[x * 3 + 0]);
            G[y * W + x] = static_cast<float>(p[x * 3 + 1]);
            R[y * W + x] = static_cast<float>(p[x * 3 + 2]);
        }
    }
}

}  // namespace

FaceDetector::FaceDetector(float confThreshold, float nmsIoU)
    : session_(L"yunet.onnx", 2),
      confThreshold_(confThreshold),
      nmsIoU_(nmsIoU) {}

std::vector<FaceBox> FaceDetector::Detect(const Image& bgr) {
    if (bgr.empty() || bgr.channels != 3) return {};

    // Letterbox: keep aspect by scaling the longer side to kInputSize and
    // padding the rest with zeros. Without this, a 640×480 frame is stretched
    // 1.33x vertically into the model input — small/distant faces become
    // ovals and detection confidence drops sharply.
    const float scale = static_cast<float>(kInputSize) /
                        std::max(bgr.width, bgr.height);
    const int newW = static_cast<int>(std::round(bgr.width * scale));
    const int newH = static_cast<int>(std::round(bgr.height * scale));
    const int padX = (kInputSize - newW) / 2;
    const int padY = (kInputSize - newH) / 2;

    Image scaled = ResizeBilinear(bgr, newW, newH);
    Image canvas(kInputSize, kInputSize, 3);
    // canvas data is zero-initialized by Image's vector ctor; just copy in.
    for (int y = 0; y < newH; ++y) {
        std::memcpy(canvas.ptr(y + padY) + padX * 3, scaled.ptr(y),
                    static_cast<size_t>(newW) * 3);
    }

    std::vector<float> chw;
    HWCtoCHW(canvas, chw);

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> shape = {1, 3, kInputSize, kInputSize};
    Ort::Value input = Ort::Value::CreateTensor<float>(memInfo, chw.data(), chw.size(),
                                                       shape.data(), shape.size());

    auto outputs = session_.session().Run(
        Ort::RunOptions{nullptr},
        session_.InputNames().data(), &input, 1,
        session_.OutputNames().data(), session_.OutputNames().size());

    // Output names are: cls_8, cls_16, cls_32, obj_8, obj_16, obj_32,
    //                   bbox_8, bbox_16, bbox_32, kps_8, kps_16, kps_32.
    // We rely on this fixed ordering established at export time.
    const int strides[3] = {8, 16, 32};
    std::vector<FaceBox> boxes;

    // Inverse letterbox: model coords -> source coords.
    const float invScale = 1.0f / scale;

    for (int li = 0; li < 3; ++li) {
        const float* cls = outputs[li].GetTensorData<float>();
        const float* obj = outputs[3 + li].GetTensorData<float>();
        const float* bbox = outputs[6 + li].GetTensorData<float>();
        const float* kps = outputs[9 + li].GetTensorData<float>();
        const int s = strides[li];
        const int gw = kInputSize / s;
        const int gh = kInputSize / s;

        for (int row = 0; row < gh; ++row) {
            for (int col = 0; col < gw; ++col) {
                int idx = row * gw + col;
                float score = cls[idx] * obj[idx];
                if (score < confThreshold_) continue;

                float cx = (col + bbox[idx * 4 + 0]) * s;
                float cy = (row + bbox[idx * 4 + 1]) * s;
                float w = std::exp(bbox[idx * 4 + 2]) * s;
                float h = std::exp(bbox[idx * 4 + 3]) * s;
                float x1 = cx - w * 0.5f;
                float y1 = cy - h * 0.5f;

                FaceBox fb;
                fb.x = (x1 - padX) * invScale;
                fb.y = (y1 - padY) * invScale;
                fb.w = w * invScale;
                fb.h = h * invScale;
                fb.score = score;
                for (int k = 0; k < 5; ++k) {
                    float kx = (kps[idx * 10 + 2 * k + 0] + col) * s;
                    float ky = (kps[idx * 10 + 2 * k + 1] + row) * s;
                    fb.kps[2 * k + 0] = (kx - padX) * invScale;
                    fb.kps[2 * k + 1] = (ky - padY) * invScale;
                }
                boxes.push_back(fb);
            }
        }
    }

    std::vector<int> keep = NMS(boxes, nmsIoU_);
    std::vector<FaceBox> out;
    out.reserve(keep.size());
    for (int i : keep) out.push_back(boxes[i]);
    return out;
}

}  // namespace face_core
