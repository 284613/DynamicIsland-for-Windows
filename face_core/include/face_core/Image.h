#pragma once
#include <cstdint>
#include <vector>

namespace face_core {

// Owning, contiguous BGR8 / RGB8 / GRAY8 image. Stride == width * channels.
// Kept deliberately tiny — we only need what the inference pipeline touches.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;  // 1 or 3
    std::vector<uint8_t> data;

    Image() = default;
    Image(int w, int h, int c) : width(w), height(h), channels(c),
                                 data(static_cast<size_t>(w) * h * c) {}

    bool empty() const { return data.empty(); }
    int stride() const { return width * channels; }
    uint8_t* ptr(int y) { return data.data() + static_cast<size_t>(y) * stride(); }
    const uint8_t* ptr(int y) const { return data.data() + static_cast<size_t>(y) * stride(); }
};

// Bilinear resize. Source must be 3-channel BGR (or RGB — caller convention).
Image ResizeBilinear(const Image& src, int dstW, int dstH);

// Reverse-mapped affine warp with bilinear sampling.
// `m` is a 2x3 row-major matrix that maps DST pixels -> SRC pixels:
//   src_x = m[0]*dx + m[1]*dy + m[2]
//   src_y = m[3]*dx + m[4]*dy + m[5]
// Out-of-range samples become zero.
Image WarpAffineBilinear(const Image& src, const float m[6], int dstW, int dstH);

// NV12 (camera-typical) -> BGR. y/uv pointers are MF buffers; pitch in bytes.
Image ConvertNV12ToBGR(const uint8_t* y, int yPitch,
                       const uint8_t* uv, int uvPitch,
                       int width, int height);

// RGB24/BGR24 byte swap in place.
void SwapRB(Image& img);

}  // namespace face_core
