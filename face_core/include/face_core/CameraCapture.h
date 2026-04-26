#pragma once
#include <memory>
#include <string>

#include "face_core/Image.h"

namespace face_core {

// Thin synchronous wrapper around Media Foundation IMFSourceReader.
// Always returns BGR24 frames (RGB32 captured then alpha stripped).
class CameraCapture {
public:
    CameraCapture();
    ~CameraCapture();

    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    // Open the default video capture device. Optional preferred dimensions
    // are best-effort hints; the driver may pick a nearby supported mode.
    // Returns false if no device is available or MF initialization fails.
    bool Open(int preferredWidth = 640, int preferredHeight = 480);

    // Block until the next frame is decoded; returns empty Image on EOS/error.
    Image ReadFrame();

    bool IsOpen() const;
    int Width() const;
    int Height() const;

    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace face_core
