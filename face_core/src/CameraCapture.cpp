#include "face_core/CameraCapture.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace face_core {

namespace {

enum class FourCC : uint32_t {
    Unknown = 0,
    YUY2 = MAKEFOURCC('Y','U','Y','2'),
    NV12 = MAKEFOURCC('N','V','1','2'),
    RGB32 = 22,   // D3DFMT_X8R8G8B8 / MFVideoFormat_RGB32 leading dword
    RGB24 = 20,
};

FourCC SubtypeToFourCC(const GUID& subtype) {
    if (subtype == MFVideoFormat_YUY2) return FourCC::YUY2;
    if (subtype == MFVideoFormat_NV12) return FourCC::NV12;
    if (subtype == MFVideoFormat_RGB32) return FourCC::RGB32;
    if (subtype == MFVideoFormat_RGB24) return FourCC::RGB24;
    return FourCC::Unknown;
}

const char* FourCCName(FourCC f) {
    switch (f) {
        case FourCC::YUY2:  return "YUY2";
        case FourCC::NV12:  return "NV12";
        case FourCC::RGB32: return "RGB32";
        case FourCC::RGB24: return "RGB24";
        default: return "?";
    }
}

uint8_t ClampByte(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

}  // namespace

struct CameraCapture::Impl {
    bool mfStarted = false;
    bool comInitialized = false;
    ComPtr<IMFSourceReader> reader;
    int width = 0;
    int height = 0;
    int stride = 0;       // negative = bottom-up
    FourCC subtype = FourCC::Unknown;

    ~Impl() {
        reader.Reset();
        if (mfStarted) MFShutdown();
        if (comInitialized) CoUninitialize();
    }

    bool Open(int preferredW, int preferredH) {
        auto fail = [](const char* where, HRESULT hr) {
            std::fprintf(stderr, "[camera] %s failed hr=0x%08lx\n", where,
                         static_cast<unsigned long>(hr));
            return false;
        };

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            comInitialized = SUCCEEDED(hr);
        } else {
            return fail("CoInitializeEx", hr);
        }
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) return fail("MFStartup", hr);
        mfStarted = true;

        ComPtr<IMFAttributes> attrs;
        hr = MFCreateAttributes(&attrs, 1);
        if (FAILED(hr)) return fail("MFCreateAttributes(enum)", hr);
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** devices = nullptr;
        UINT32 deviceCount = 0;
        hr = MFEnumDeviceSources(attrs.Get(), &devices, &deviceCount);
        if (FAILED(hr)) {
            if (devices) CoTaskMemFree(devices);
            return fail("MFEnumDeviceSources", hr);
        }
        if (deviceCount == 0) {
            if (devices) CoTaskMemFree(devices);
            std::fprintf(stderr, "[camera] no video capture devices found\n");
            return false;
        }
        std::fprintf(stderr, "[camera] %u device(s) found\n", deviceCount);

        // Try each device until one activates and yields a usable stream.
        ComPtr<IMFMediaSource> source;
        for (UINT32 i = 0; i < deviceCount; ++i) {
            wchar_t* name = nullptr;
            UINT32 nameLen = 0;
            devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen);
            std::fprintf(stderr, "[camera] device[%u] = %ls\n", i, name ? name : L"<unnamed>");
            if (name) CoTaskMemFree(name);

            HRESULT actHr = devices[i]->ActivateObject(IID_PPV_ARGS(&source));
            if (SUCCEEDED(actHr)) {
                std::fprintf(stderr, "[camera] activated device[%u]\n", i);
                break;
            }
            std::fprintf(stderr, "[camera] device[%u] activate failed hr=0x%08lx\n",
                         i, static_cast<unsigned long>(actHr));
            source.Reset();
        }
        for (UINT32 i = 0; i < deviceCount; ++i) devices[i]->Release();
        CoTaskMemFree(devices);
        if (!source) return fail("ActivateObject (all devices)", E_FAIL);

        ComPtr<IMFAttributes> rdAttrs;
        MFCreateAttributes(&rdAttrs, 1);
        rdAttrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);

        hr = MFCreateSourceReaderFromMediaSource(source.Get(), rdAttrs.Get(), &reader);
        if (FAILED(hr)) return fail("MFCreateSourceReaderFromMediaSource", hr);

        // Pick a native media type from the device's enumerated set whose
        // subtype we can convert to BGR. Prefer YUY2 / NV12 / RGB32 / RGB24
        // and a frame size as close to the request as possible.
        ComPtr<IMFMediaType> chosen;
        FourCC chosenFourCC = FourCC::Unknown;
        UINT32 chosenW = 0, chosenH = 0;
        int bestScore = INT_MIN;
        for (DWORD ti = 0;; ++ti) {
            ComPtr<IMFMediaType> nt;
            HRESULT eh = reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                    ti, &nt);
            if (eh == MF_E_NO_MORE_TYPES) break;
            if (FAILED(eh)) break;

            GUID sub{};
            nt->GetGUID(MF_MT_SUBTYPE, &sub);
            FourCC fc = SubtypeToFourCC(sub);
            if (fc == FourCC::Unknown) continue;

            UINT32 w = 0, h = 0;
            MFGetAttributeSize(nt.Get(), MF_MT_FRAME_SIZE, &w, &h);

            // Score: prefer matching the requested size; mild bias toward YUY2/NV12.
            int score = -static_cast<int>(std::abs(static_cast<int>(w) - preferredW) +
                                          std::abs(static_cast<int>(h) - preferredH));
            if (fc == FourCC::YUY2 || fc == FourCC::NV12) score += 50;
            if (fc == FourCC::RGB32 || fc == FourCC::RGB24) score += 10;

            std::fprintf(stderr, "[camera]  native[%lu] %s %ux%u score=%d\n",
                         ti, FourCCName(fc), w, h, score);

            if (score > bestScore) {
                bestScore = score;
                chosen = nt;
                chosenFourCC = fc;
                chosenW = w;
                chosenH = h;
            }
        }
        if (!chosen) {
            std::fprintf(stderr, "[camera] no acceptable native media type (need YUY2/NV12/RGB)\n");
            return false;
        }
        std::fprintf(stderr, "[camera] selected %s %ux%u\n",
                     FourCCName(chosenFourCC), chosenW, chosenH);

        hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                         nullptr, chosen.Get());
        if (FAILED(hr)) return fail("SetCurrentMediaType(native)", hr);

        ComPtr<IMFMediaType> negotiated;
        if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               &negotiated))) {
            return fail("GetCurrentMediaType", E_FAIL);
        }
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(negotiated.Get(), MF_MT_FRAME_SIZE, &w, &h);
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        subtype = chosenFourCC;

        // Stride is only meaningful for packed formats; for NV12 the buffer
        // contains plane Y followed by interleaved UV.
        LONG defaultStride = 0;
        UINT32 fourcc = static_cast<UINT32>(chosenFourCC);
        if (FAILED(MFGetStrideForBitmapInfoHeader(fourcc, w, &defaultStride))) {
            switch (chosenFourCC) {
                case FourCC::YUY2:  defaultStride = static_cast<LONG>(w * 2); break;
                case FourCC::NV12:  defaultStride = static_cast<LONG>(w);     break;
                case FourCC::RGB32: defaultStride = static_cast<LONG>(w * 4); break;
                case FourCC::RGB24: defaultStride = static_cast<LONG>(w * 3); break;
                default:            defaultStride = static_cast<LONG>(w * 4); break;
            }
        }
        stride = defaultStride;

        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        return true;
    }

    Image ConvertYUY2(const uint8_t* data, int rowStride) const {
        Image img(width, height, 3);
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = data + static_cast<size_t>(y) * rowStride;
            uint8_t* drow = img.ptr(y);
            for (int x = 0; x + 1 < width; x += 2) {
                int Y0 = row[x * 2 + 0];
                int U  = row[x * 2 + 1];
                int Y1 = row[x * 2 + 2];
                int V  = row[x * 2 + 3];
                int C0 = Y0 - 16;
                int C1 = Y1 - 16;
                int D = U - 128;
                int E = V - 128;
                int r0 = (298 * C0 + 409 * E + 128) >> 8;
                int g0 = (298 * C0 - 100 * D - 208 * E + 128) >> 8;
                int b0 = (298 * C0 + 516 * D + 128) >> 8;
                int r1 = (298 * C1 + 409 * E + 128) >> 8;
                int g1 = (298 * C1 - 100 * D - 208 * E + 128) >> 8;
                int b1 = (298 * C1 + 516 * D + 128) >> 8;
                drow[x * 3 + 0] = ClampByte(b0);
                drow[x * 3 + 1] = ClampByte(g0);
                drow[x * 3 + 2] = ClampByte(r0);
                drow[(x + 1) * 3 + 0] = ClampByte(b1);
                drow[(x + 1) * 3 + 1] = ClampByte(g1);
                drow[(x + 1) * 3 + 2] = ClampByte(r1);
            }
        }
        return img;
    }

    Image ConvertNV12(const uint8_t* data) const {
        // Y plane: width * height; UV plane immediately after.
        int yPitch = width;
        int uvPitch = width;
        return ConvertNV12ToBGR(data, yPitch,
                                data + static_cast<size_t>(yPitch) * height,
                                uvPitch, width, height);
    }

    Image ConvertRGB32(const uint8_t* data) const {
        Image img(width, height, 3);
        const int absStride = std::abs(stride);
        const bool topDown = stride > 0;
        for (int y = 0; y < height; ++y) {
            int srcY = topDown ? y : (height - 1 - y);
            const uint8_t* row = data + static_cast<size_t>(srcY) * absStride;
            uint8_t* drow = img.ptr(y);
            for (int x = 0; x < width; ++x) {
                drow[x * 3 + 0] = row[x * 4 + 0];
                drow[x * 3 + 1] = row[x * 4 + 1];
                drow[x * 3 + 2] = row[x * 4 + 2];
            }
        }
        return img;
    }

    Image ConvertRGB24(const uint8_t* data) const {
        Image img(width, height, 3);
        const int absStride = std::abs(stride);
        const bool topDown = stride > 0;
        for (int y = 0; y < height; ++y) {
            int srcY = topDown ? y : (height - 1 - y);
            const uint8_t* row = data + static_cast<size_t>(srcY) * absStride;
            std::memcpy(img.ptr(y), row, static_cast<size_t>(width) * 3);
        }
        return img;
    }

    Image ReadFrame() {
        if (!reader) return {};
        DWORD streamIndex = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        for (int attempt = 0; attempt < 4; ++attempt) {
            HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                            &streamIndex, &flags, &ts, &sample);
            if (FAILED(hr)) return {};
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return {};
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                ComPtr<IMFMediaType> nt;
                reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &nt);
                UINT32 w, h;
                MFGetAttributeSize(nt.Get(), MF_MT_FRAME_SIZE, &w, &h);
                width = w; height = h;
                continue;
            }
            if (sample) break;
        }
        if (!sample) return {};

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return {};
        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) return {};

        Image img;
        switch (subtype) {
            case FourCC::YUY2:  img = ConvertYUY2(data, std::abs(stride)); break;
            case FourCC::NV12:  img = ConvertNV12(data);                   break;
            case FourCC::RGB32: img = ConvertRGB32(data);                  break;
            case FourCC::RGB24: img = ConvertRGB24(data);                  break;
            default: break;
        }
        buffer->Unlock();
        return img;
    }
};

CameraCapture::CameraCapture() : impl_(std::make_unique<Impl>()) {}
CameraCapture::~CameraCapture() = default;

bool CameraCapture::Open(int w, int h) { return impl_->Open(w, h); }
Image CameraCapture::ReadFrame() { return impl_->ReadFrame(); }
bool CameraCapture::IsOpen() const { return impl_ && impl_->reader; }
int CameraCapture::Width() const { return impl_ ? impl_->width : 0; }
int CameraCapture::Height() const { return impl_ ? impl_->height : 0; }
void CameraCapture::Close() { impl_.reset(new Impl()); }

}  // namespace face_core
