#include "face_core/Image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace face_core {

Image ResizeBilinear(const Image& src, int dstW, int dstH) {
    Image dst(dstW, dstH, src.channels);
    if (src.empty() || dstW <= 0 || dstH <= 0) return dst;
    const float fx = static_cast<float>(src.width) / dstW;
    const float fy = static_cast<float>(src.height) / dstH;
    const int c = src.channels;
    for (int y = 0; y < dstH; ++y) {
        float sy = (y + 0.5f) * fy - 0.5f;
        int y0 = static_cast<int>(std::floor(sy));
        int y1 = y0 + 1;
        float wy1 = sy - y0;
        float wy0 = 1.0f - wy1;
        y0 = std::clamp(y0, 0, src.height - 1);
        y1 = std::clamp(y1, 0, src.height - 1);
        const uint8_t* row0 = src.ptr(y0);
        const uint8_t* row1 = src.ptr(y1);
        uint8_t* drow = dst.ptr(y);
        for (int x = 0; x < dstW; ++x) {
            float sx = (x + 0.5f) * fx - 0.5f;
            int x0 = static_cast<int>(std::floor(sx));
            int x1 = x0 + 1;
            float wx1 = sx - x0;
            float wx0 = 1.0f - wx1;
            x0 = std::clamp(x0, 0, src.width - 1);
            x1 = std::clamp(x1, 0, src.width - 1);
            for (int k = 0; k < c; ++k) {
                float v = wy0 * (wx0 * row0[x0 * c + k] + wx1 * row0[x1 * c + k]) +
                          wy1 * (wx0 * row1[x0 * c + k] + wx1 * row1[x1 * c + k]);
                drow[x * c + k] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
        }
    }
    return dst;
}

Image WarpAffineBilinear(const Image& src, const float m[6], int dstW, int dstH) {
    Image dst(dstW, dstH, src.channels);
    if (src.empty() || dstW <= 0 || dstH <= 0) return dst;
    const int c = src.channels;
    for (int y = 0; y < dstH; ++y) {
        uint8_t* drow = dst.ptr(y);
        for (int x = 0; x < dstW; ++x) {
            float sx = m[0] * x + m[1] * y + m[2];
            float sy = m[3] * x + m[4] * y + m[5];
            int x0 = static_cast<int>(std::floor(sx));
            int y0 = static_cast<int>(std::floor(sy));
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            float wx1 = sx - x0;
            float wy1 = sy - y0;
            float wx0 = 1.0f - wx1;
            float wy0 = 1.0f - wy1;
            // Out-of-bounds -> 0
            if (x0 < 0 || x1 >= src.width || y0 < 0 || y1 >= src.height) {
                std::memset(drow + x * c, 0, c);
                continue;
            }
            const uint8_t* r0 = src.ptr(y0);
            const uint8_t* r1 = src.ptr(y1);
            for (int k = 0; k < c; ++k) {
                float v = wy0 * (wx0 * r0[x0 * c + k] + wx1 * r0[x1 * c + k]) +
                          wy1 * (wx0 * r1[x0 * c + k] + wx1 * r1[x1 * c + k]);
                drow[x * c + k] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
        }
    }
    return dst;
}

Image ConvertNV12ToBGR(const uint8_t* y, int yPitch,
                       const uint8_t* uv, int uvPitch,
                       int width, int height) {
    Image dst(width, height, 3);
    for (int j = 0; j < height; ++j) {
        const uint8_t* yRow = y + j * yPitch;
        const uint8_t* uvRow = uv + (j / 2) * uvPitch;
        uint8_t* dRow = dst.ptr(j);
        for (int i = 0; i < width; ++i) {
            int Y = yRow[i];
            int U = uvRow[(i & ~1)] - 128;
            int V = uvRow[(i & ~1) + 1] - 128;
            // BT.601 limited-range -> full BGR
            int C = Y - 16;
            if (C < 0) C = 0;
            int r = (298 * C + 409 * V + 128) >> 8;
            int g = (298 * C - 100 * U - 208 * V + 128) >> 8;
            int b = (298 * C + 516 * U + 128) >> 8;
            dRow[i * 3 + 0] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            dRow[i * 3 + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            dRow[i * 3 + 2] = static_cast<uint8_t>(std::clamp(r, 0, 255));
        }
    }
    return dst;
}

void SwapRB(Image& img) {
    if (img.channels != 3) return;
    for (int y = 0; y < img.height; ++y) {
        uint8_t* p = img.ptr(y);
        for (int x = 0; x < img.width; ++x) {
            std::swap(p[x * 3 + 0], p[x * 3 + 2]);
        }
    }
}

}  // namespace face_core
