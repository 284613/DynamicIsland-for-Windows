#include "face_core/FaceLandmarks3D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <stdexcept>
#include <vector>

namespace face_core {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::vector<uint8_t> ReadAllBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return in ? bytes : std::vector<uint8_t>{};
}

size_t FindBinUnicodeKey(const std::vector<uint8_t>& bytes, const char* key, size_t keyLen) {
    std::vector<uint8_t> pattern;
    pattern.push_back(0x58);  // BINUNICODE
    pattern.push_back(static_cast<uint8_t>(keyLen));
    pattern.push_back(0);
    pattern.push_back(0);
    pattern.push_back(0);
    pattern.insert(pattern.end(), key, key + keyLen);

    auto it = std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end());
    if (it == bytes.end()) return std::string::npos;
    return static_cast<size_t>(std::distance(bytes.begin(), it)) + pattern.size();
}

void ExtractFloatBlock(const std::vector<uint8_t>& bytes, const char* key,
                       std::array<float, 62>& out) {
    constexpr size_t kFloatBytes = sizeof(float) * 62;
    size_t pos = FindBinUnicodeKey(bytes, key, std::char_traits<char>::length(key));
    if (pos == std::string::npos) {
        throw std::runtime_error("3DDFA stats pickle: missing key");
    }

    for (size_t i = pos; i + 2 + kFloatBytes <= bytes.size(); ++i) {
        if (bytes[i] == 0x43 && bytes[i + 1] == kFloatBytes) {  // SHORT_BINBYTES
            std::copy(bytes.begin() + i + 2, bytes.begin() + i + 2 + kFloatBytes,
                      reinterpret_cast<uint8_t*>(out.data()));
            return;
        }
    }
    throw std::runtime_error("3DDFA stats pickle: missing 62-float block");
}

Image CropPaddedRoi(const Image& src, float sx, float sy, float ex, float ey) {
    if (src.empty() || src.channels != 3) return {};
    const int ix0 = static_cast<int>(std::round(sx));
    const int iy0 = static_cast<int>(std::round(sy));
    const int ix1 = static_cast<int>(std::round(ex));
    const int iy1 = static_cast<int>(std::round(ey));
    const int w = ix1 - ix0;
    const int h = iy1 - iy0;
    if (w <= 1 || h <= 1) return {};

    Image out(w, h, 3);
    const int srcX0 = std::max(0, ix0);
    const int srcY0 = std::max(0, iy0);
    const int srcX1 = std::min(src.width, ix1);
    const int srcY1 = std::min(src.height, iy1);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) return out;

    const int dstX0 = srcX0 - ix0;
    const int dstY0 = srcY0 - iy0;
    const int copyW = srcX1 - srcX0;
    const int copyH = srcY1 - srcY0;
    for (int y = 0; y < copyH; ++y) {
        const uint8_t* s = src.ptr(srcY0 + y) + srcX0 * 3;
        uint8_t* d = out.ptr(dstY0 + y) + dstX0 * 3;
        std::copy(s, s + static_cast<size_t>(copyW) * 3, d);
    }
    return out;
}

Image Crop3DDFARoi(const Image& src, const FaceBox& box) {
    const float left = box.x;
    const float top = box.y;
    const float right = box.x + box.w;
    const float bottom = box.y + box.h;
    const float oldSize = (right - left + bottom - top) * 0.5f;
    const float centerX = right - (right - left) * 0.5f;
    const float centerY = bottom - (bottom - top) * 0.5f + oldSize * 0.14f;
    const float size = static_cast<float>(static_cast<int>(oldSize * 1.58f));
    const float sx = centerX - size * 0.5f;
    const float sy = centerY - size * 0.5f;
    return CropPaddedRoi(src, sx, sy, sx + size, sy + size);
}

void BgrTo3DDFAInput(const Image& img, std::vector<float>& out) {
    const int w = img.width;
    const int h = img.height;
    out.resize(static_cast<size_t>(3) * w * h);
    float* b = out.data();
    float* g = b + static_cast<size_t>(w) * h;
    float* r = g + static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = img.ptr(y);
        for (int x = 0; x < w; ++x) {
            b[y * w + x] = (static_cast<float>(row[x * 3 + 0]) - 127.5f) / 128.0f;
            g[y * w + x] = (static_cast<float>(row[x * 3 + 1]) - 127.5f) / 128.0f;
            r[y * w + x] = (static_cast<float>(row[x * 3 + 2]) - 127.5f) / 128.0f;
        }
    }
}

float Norm3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void Cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float Degrees(float radians) {
    return radians * 180.0f / kPi;
}

}  // namespace

FaceLandmarks3D::FaceLandmarks3D() : session_(L"3ddfa_v2.onnx", 2) {
    LoadParamStats();
}

bool FaceLandmarks3D::Run(const Image& bgr, const FaceBox& box,
                          std::array<float, 62>& outParams) {
    Image roi = Crop3DDFARoi(bgr, box);
    if (roi.empty()) return false;

    Image resized = ResizeBilinear(roi, 120, 120);
    std::vector<float> chw;
    BgrTo3DDFAInput(resized, chw);

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> shape = {1, 3, 120, 120};
    Ort::Value input = Ort::Value::CreateTensor<float>(memInfo, chw.data(), chw.size(),
                                                       shape.data(), shape.size());

    auto outputs = session_.session().Run(
        Ort::RunOptions{nullptr},
        session_.InputNames().data(), &input, 1,
        session_.OutputNames().data(), session_.OutputNames().size());

    if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != outParams.size()) {
        return false;
    }
    const float* raw = outputs[0].GetTensorData<float>();
    for (size_t i = 0; i < outParams.size(); ++i) {
        outParams[i] = raw[i] * std_[i] + mean_[i];
    }
    return true;
}

HeadPose FaceLandmarks3D::ExtractPose(const std::array<float, 62>& params) {
    float r1[3] = {params[0], params[1], params[2]};
    float r2[3] = {params[4], params[5], params[6]};
    float n1 = Norm3(r1);
    float n2 = Norm3(r2);
    if (n1 < 1e-8f || n2 < 1e-8f) return {};

    for (float& v : r1) v /= n1;
    for (float& v : r2) v /= n2;
    float r3[3];
    Cross3(r1, r2, r3);

    float R[3][3] = {
        {r1[0], r1[1], r1[2]},
        {r2[0], r2[1], r2[2]},
        {r3[0], r3[1], r3[2]},
    };

    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    if (R[2][0] > 0.998f) {
        yaw = kPi * 0.5f;
        pitch = std::atan2(-R[0][1], -R[0][2]);
    } else if (R[2][0] < -0.998f) {
        yaw = -kPi * 0.5f;
        pitch = std::atan2(R[0][1], R[0][2]);
    } else {
        yaw = std::asin(std::clamp(R[2][0], -1.0f, 1.0f));
        float c = std::cos(yaw);
        pitch = std::atan2(R[2][1] / c, R[2][2] / c);
        roll = std::atan2(R[1][0] / c, R[0][0] / c);
    }

    return {Degrees(yaw), Degrees(pitch), Degrees(roll)};
}

void FaceLandmarks3D::LoadParamStats() {
    std::wstring path = ResolveModelPath(L"3ddfa_param_mean_std.pkl");
    if (path.empty()) {
        throw std::runtime_error("3DDFA stats pickle not found: 3ddfa_param_mean_std.pkl");
    }
    std::vector<uint8_t> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        throw std::runtime_error("3DDFA stats pickle could not be read");
    }

    ExtractFloatBlock(bytes, "mean", mean_);
    ExtractFloatBlock(bytes, "std", std_);
}

}  // namespace face_core
