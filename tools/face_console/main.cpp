#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "face_core/face_core.h"

using namespace face_core;

namespace {

double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

std::string NarrowAscii(const wchar_t* text) {
    std::string out;
    if (!text) return out;
    for (const wchar_t* p = text; *p; ++p) {
        if (*p < 32 || *p > 126) return {};
        out.push_back(static_cast<char>(*p));
    }
    return out;
}

std::wstring WidenAscii(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

const wchar_t* StageName(PipelineStage stage) {
    switch (stage) {
    case PipelineStage::Idle: return L"idle";
    case PipelineStage::Detecting: return L"detect";
    case PipelineStage::Challenging: return L"challenge";
    case PipelineStage::Verifying: return L"live";
    case PipelineStage::Identifying: return L"ident";
    case PipelineStage::Success: return L"success";
    case PipelineStage::Failed: return L"failed";
    }
    return L"unknown";
}

std::optional<FaceBox> BestFace(FaceDetector& detector, const Image& frame) {
    std::vector<FaceBox> faces = detector.Detect(frame);
    if (faces.empty()) return std::nullopt;
    return *std::max_element(faces.begin(), faces.end(),
                             [](const FaceBox& a, const FaceBox& b) {
                                 return a.score < b.score;
                             });
}

bool IsPoseForAngle(uint8_t tag, const HeadPose& pose) {
    switch (tag) {
    case 0: return std::fabs(pose.yaw) < 5.0f && std::fabs(pose.pitch) < 10.0f;
    case 1: return pose.yaw < -12.0f;
    case 2: return pose.yaw > 12.0f;
    default: return false;
    }
}

const wchar_t* AngleName(uint8_t tag) {
    switch (tag) {
    case 0: return L"front";
    case 1: return L"left";
    case 2: return L"right";
    default: return L"unknown";
    }
}

struct RunOpts {
    // Empirically the best balance on a typical 480p–720p UVC webcam: same
    // detection accuracy as 480p, fewer false positives than both 480p and
    // 720p, ~half the inference cost of 720p.
    int camW = 800;
    int camH = 600;
    float detThreshold = 0.6f;
    float matchThreshold = 0.45f;
    bool strict = false;  // --strict re-enables turn-head active liveness
};

bool ParseOpts(int argc, wchar_t** argv, int firstFlag, RunOpts& opts) {
    for (int i = firstFlag; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--cam" && i + 1 < argc) {
            std::wstring v = argv[++i];
            // Accept "WxH" or "640x480" or shorthand "480p" / "720p" / "600p".
            if (v == L"480p")      { opts.camW = 640;  opts.camH = 480; continue; }
            if (v == L"600p")      { opts.camW = 800;  opts.camH = 600; continue; }
            if (v == L"720p")      { opts.camW = 1280; opts.camH = 720; continue; }
            size_t xPos = v.find(L'x');
            if (xPos == std::wstring::npos) {
                std::wcerr << L"--cam expects WxH or 480p/600p/720p\n";
                return false;
            }
            opts.camW = std::stoi(v.substr(0, xPos));
            opts.camH = std::stoi(v.substr(xPos + 1));
        } else if (a == L"--det" && i + 1 < argc) {
            opts.detThreshold = std::stof(argv[++i]);
        } else if (a == L"--match" && i + 1 < argc) {
            opts.matchThreshold = std::stof(argv[++i]);
        } else if (a == L"--strict") {
            opts.strict = true;
        } else {
            std::wcerr << L"Unknown option: " << a << L"\n";
            return false;
        }
    }
    std::wcout << L"[opts] cam=" << opts.camW << L"x" << opts.camH
               << L" det=" << std::fixed << std::setprecision(2) << opts.detThreshold
               << L" match=" << opts.matchThreshold
               << (opts.strict ? L" strict=on" : L" strict=off")
               << L"\n";
    return true;
}

void PrintUsage() {
    std::wcout
        << L"face_console commands:\n"
        << L"  face_console selftest\n"
        << L"  face_console list\n"
        << L"  face_console snap <out.bmp> [--cam WxH]\n"
        << L"  face_console probe                [--cam WxH] [--det 0.5]\n"
        << L"  face_console enroll <ascii-name>  [--cam WxH] [--det 0.5]\n"
        << L"  face_console verify               [--cam WxH] [--det 0.5] [--match 0.42] [--strict]\n"
        << L"  face_console clear\n"
        << L"\n"
        << L"  --cam shorthand: 480p (640x480) | 600p (800x600) | 720p (1280x720)\n"
        << L"  defaults: --cam 600p --det 0.6 --match 0.45\n";
}

bool WriteBmp24(const std::wstring& path, const Image& bgr) {
    if (bgr.empty() || bgr.channels != 3) return false;
    const int W = bgr.width;
    const int H = bgr.height;
    const int rowBytes = ((W * 3 + 3) / 4) * 4;
    const int padBytes = rowBytes - W * 3;
    const int pixelBytes = rowBytes * H;
#pragma pack(push, 1)
    struct BMPFile { uint16_t bf; uint32_t size; uint16_t r1, r2; uint32_t off; } fh{};
    struct BMPInfo {
        uint32_t hSize; int32_t w, h; uint16_t planes, bpp;
        uint32_t comp, image, ppmX, ppmY, used, important;
    } ih{};
#pragma pack(pop)
    fh.bf = 0x4D42;
    fh.off = sizeof(fh) + sizeof(ih);
    fh.size = fh.off + pixelBytes;
    ih.hSize = sizeof(ih);
    ih.w = W; ih.h = H;
    ih.planes = 1; ih.bpp = 24;
    ih.image = pixelBytes;
    ih.ppmX = ih.ppmY = 2835;
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) return false;
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&ih, sizeof(ih), 1, f);
    static const uint8_t padBuf[3] = {0, 0, 0};
    for (int y = H - 1; y >= 0; --y) {  // BMP is bottom-up
        std::fwrite(bgr.ptr(y), 1, static_cast<size_t>(W) * 3, f);
        if (padBytes > 0) std::fwrite(padBuf, 1, padBytes, f);
    }
    std::fclose(f);
    return true;
}

int RunSnap(const std::wstring& outPath, const RunOpts& opts) {
    CameraCapture camera;
    if (!camera.Open(opts.camW, opts.camH)) {
        std::wcerr << L"Could not open the default camera.\n";
        return 3;
    }
    // Discard a few frames so AE/AWB stabilizes.
    for (int i = 0; i < 5; ++i) camera.ReadFrame();
    Image frame = camera.ReadFrame();
    if (frame.empty()) {
        std::wcerr << L"Camera returned an empty frame.\n";
        return 3;
    }
    if (!WriteBmp24(outPath, frame)) {
        std::wcerr << L"Could not write BMP: " << outPath << L"\n";
        return 4;
    }
    std::wcout << L"Wrote " << frame.width << L"x" << frame.height
               << L" BGR frame to " << outPath << L"\n";
    return 0;
}

int RunProbe(const RunOpts& opts) {
    CameraCapture camera;
    if (!camera.Open(opts.camW, opts.camH)) {
        std::wcerr << L"Could not open the default camera.\n";
        return 3;
    }
    FaceDetector strict(opts.detThreshold);
    FaceDetector loose(0.10f);
    std::wcout << L"Press Ctrl+C to stop.\n";
    int frameCount = 0;
    while (true) {
        Image frame = camera.ReadFrame();
        if (frame.empty()) {
            std::wcerr << L"empty frame\n"; return 3;
        }
        ++frameCount;
        auto strictBoxes = strict.Detect(frame);
        auto looseBoxes = loose.Detect(frame);
        float bestLoose = 0.0f;
        for (auto& b : looseBoxes) bestLoose = std::max(bestLoose, b.score);

        // Pixel sanity: middle-row average B/G/R to detect the channel order.
        const uint8_t* mid = frame.ptr(frame.height / 2);
        double avgB = 0, avgG = 0, avgR = 0;
        for (int x = 0; x < frame.width; ++x) {
            avgB += mid[x * 3 + 0];
            avgG += mid[x * 3 + 1];
            avgR += mid[x * 3 + 2];
        }
        avgB /= frame.width; avgG /= frame.width; avgR /= frame.width;

        std::wcout << L"frame=" << frameCount
                   << L" size=" << frame.width << L"x" << frame.height
                   << L" mid_BGR=(" << static_cast<int>(avgB) << L","
                   << static_cast<int>(avgG) << L"," << static_cast<int>(avgR) << L")"
                   << L" det@" << std::fixed << std::setprecision(2) << opts.detThreshold
                   << L"=" << strictBoxes.size()
                   << L" det@0.10=" << looseBoxes.size()
                   << L" topScore=" << std::fixed << std::setprecision(3) << bestLoose
                   << L"\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

int RunSelfTest() {
    std::wcout << L"[selftest] loading models\n";
    FaceDetector detector(0.99f);
    LivenessDetector liveness;
    FaceLandmarks3D landmarks;
    FaceRecognizer recognizer;
    FaceTemplateStore store;
    bool loaded = store.Load();

    Image frame(640, 480, 3);
    FaceBox box;
    box.x = 220.0f;
    box.y = 110.0f;
    box.w = 200.0f;
    box.h = 240.0f;
    box.score = 1.0f;
    box.kps[0] = 260.0f; box.kps[1] = 200.0f;
    box.kps[2] = 380.0f; box.kps[3] = 200.0f;
    box.kps[4] = 320.0f; box.kps[5] = 255.0f;
    box.kps[6] = 275.0f; box.kps[7] = 320.0f;
    box.kps[8] = 365.0f; box.kps[9] = 320.0f;

    std::vector<FaceBox> faces = detector.Detect(frame);
    float real = liveness.Score(frame, box);
    std::array<float, 62> params{};
    bool poseOk = landmarks.Run(frame, box, params);
    HeadPose pose = poseOk ? FaceLandmarks3D::ExtractPose(params) : HeadPose{};
    Image aligned = AlignArcFace(frame, box.kps);
    Embedding emb{};
    bool embOk = recognizer.Embed(aligned, emb);

    std::wcout << L"[selftest] detector output count=" << faces.size() << L"\n";
    std::wcout << L"[selftest] silent-face real=" << std::fixed << std::setprecision(3)
               << real << L"\n";
    std::wcout << L"[selftest] 3ddfa=" << (poseOk ? L"ok" : L"failed")
               << L" yaw=" << pose.yaw << L" pitch=" << pose.pitch
               << L" roll=" << pose.roll << L"\n";
    std::wcout << L"[selftest] arcface=" << (embOk ? L"ok" : L"failed") << L"\n";
    std::wcout << L"[selftest] store=" << (loaded ? L"ok" : L"load failed")
               << L" templates=" << store.Count() << L"\n";

    return loaded && poseOk && embOk ? 0 : 2;
}

int RunList() {
    FaceTemplateStore store;
    if (!store.Load()) {
        std::wcerr << L"Could not load face store: " << store.Path() << L"\n";
        return 2;
    }
    std::vector<std::string> names = store.ListNames();
    std::wcout << L"Store: " << store.Path() << L"\n";
    std::wcout << L"Templates: " << store.Count() << L"\n";
    if (names.empty()) {
        std::wcout << L"No enrolled identities.\n";
        return 0;
    }
    for (const std::string& name : names) {
        std::wcout << L"  " << WidenAscii(name) << L"  templates="
                   << store.CountForName(name) << L"\n";
    }
    return 0;
}

int RunClear() {
    std::wcout << L"Type CLEAR to delete all enrolled face templates: ";
    std::wstring answer;
    std::getline(std::wcin, answer);
    if (answer != L"CLEAR") {
        std::wcout << L"Cancelled.\n";
        return 0;
    }
    FaceTemplateStore store;
    if (!store.Load()) {
        std::wcerr << L"Could not load face store.\n";
        return 2;
    }
    store.Clear();
    store.Save();
    std::wcout << L"Face store cleared.\n";
    return 0;
}

int RunEnroll(const std::string& name, const RunOpts& opts) {
    if (name.empty() || name.size() > 32) {
        std::wcerr << L"Enrollment name must be 1..32 printable ASCII characters.\n";
        return 2;
    }

    FaceTemplateStore store;
    if (!store.Load()) {
        std::wcerr << L"Could not load existing face store.\n";
        return 2;
    }
    store.Remove(name);

    CameraCapture camera;
    if (!camera.Open(opts.camW, opts.camH)) {
        std::wcerr << L"Could not open the default camera.\n";
        return 3;
    }

    FaceDetector detector(opts.detThreshold);
    LivenessDetector liveness;
    FaceLandmarks3D landmarks;
    FaceRecognizer recognizer;

    std::wcout << L"Enrollment started for " << WidenAscii(name) << L". Press Ctrl+C to cancel.\n";
    for (uint8_t tag : {static_cast<uint8_t>(0), static_cast<uint8_t>(1), static_cast<uint8_t>(2)}) {
        int captured = 0;
        double lastCapture = 0.0;
        int frameCount = 0;
        std::wcout << L"[enroll] target=" << AngleName(tag) << L"\n";

        while (captured < 2) {
            Image frame = camera.ReadFrame();
            if (frame.empty()) {
                std::wcerr << L"Camera frame read failed.\n";
                return 3;
            }

            ++frameCount;
            auto best = BestFace(detector, frame);
            if (!best) {
                if (frameCount % 15 == 0) std::wcout << L"[enroll] no face\n";
                continue;
            }

            float real = liveness.Score(frame, *best);
            std::array<float, 62> params{};
            if (real < LivenessDetector::kDefaultRealThreshold || !landmarks.Run(frame, *best, params)) {
                if (frameCount % 15 == 0) {
                    std::wcout << L"[enroll] waiting: live=" << std::fixed << std::setprecision(2)
                               << real << L"\n";
                }
                continue;
            }

            HeadPose pose = FaceLandmarks3D::ExtractPose(params);
            if (!IsPoseForAngle(tag, pose)) {
                if (frameCount % 15 == 0) {
                    std::wcout << L"[enroll] pose yaw=" << std::fixed << std::setprecision(1)
                               << pose.yaw << L" pitch=" << pose.pitch
                               << L" target=" << AngleName(tag) << L"\n";
                }
                continue;
            }

            double now = NowSeconds();
            if (now - lastCapture < 0.25) continue;

            Image aligned = AlignArcFace(frame, best->kps);
            Embedding emb{};
            if (aligned.empty() || !recognizer.Embed(aligned, emb)) {
                std::wcout << L"[enroll] alignment failed\n";
                continue;
            }

            store.Add(FaceTemplate{name, tag, emb});
            lastCapture = now;
            ++captured;
            std::wcout << L"[enroll] captured " << AngleName(tag) << L" "
                       << captured << L"/2 live=" << std::fixed << std::setprecision(2)
                       << real << L" yaw=" << pose.yaw << L"\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    store.Save();
    std::wcout << L"Enrollment complete. templates=" << store.CountForName(name) << L"\n";
    return 0;
}

int RunVerify(const RunOpts& opts) {
    auto store = std::make_shared<FaceTemplateStore>();
    if (!store->Load()) {
        std::wcerr << L"Could not load face store.\n";
        return 2;
    }
    if (store->Count() == 0) {
        std::wcout << L"Warning: no enrolled templates. Verification cannot match.\n";
    }

    CameraCapture camera;
    if (!camera.Open(opts.camW, opts.camH)) {
        std::wcerr << L"Could not open the default camera.\n";
        return 3;
    }

    FacePipeline::Config cfg;
    cfg.matchThreshold = opts.matchThreshold;
    cfg.requireActiveLiveness = opts.strict;
    FacePipeline pipeline(store, cfg);
    std::wcout << L"Verification started. Stops on first Success/Failed.\n";

    PipelineStage lastStage = PipelineStage::Idle;
    int finalCode = 0;
    while (true) {
        Image frame = camera.ReadFrame();
        if (frame.empty()) {
            std::wcerr << L"Camera frame read failed.\n";
            return 3;
        }

        double now = NowSeconds();
        bool running = pipeline.OnFrame(frame, now, [&](const PipelineEvent& ev) {
            // Only print on stage change OR terminal outcomes — eliminates the
            // per-frame chatter that made the loop unreadable.
            bool terminal =
                ev.stage == PipelineStage::Success || ev.stage == PipelineStage::Failed;
            if (!terminal && ev.stage == lastStage) return;
            lastStage = ev.stage;
            std::wcout << L"[" << StageName(ev.stage) << L"] " << ev.hint
                       << L" score=" << std::fixed << std::setprecision(3) << ev.score;
            if (!ev.identity.empty()) std::wcout << L" identity=" << WidenAscii(ev.identity);
            std::wcout << L"\n";
            if (ev.stage == PipelineStage::Failed) finalCode = 5;
        });

        if (!running) break;
    }
    return finalCode;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2) {
            PrintUsage();
            return 1;
        }

        std::wstring cmd = argv[1];
        if (cmd == L"selftest") return RunSelfTest();
        if (cmd == L"list") return RunList();
        if (cmd == L"clear") return RunClear();

        RunOpts opts;
        if (cmd == L"verify") {
            if (!ParseOpts(argc, argv, 2, opts)) return 1;
            return RunVerify(opts);
        }
        if (cmd == L"probe") {
            if (!ParseOpts(argc, argv, 2, opts)) return 1;
            return RunProbe(opts);
        }
        if (cmd == L"snap") {
            if (argc < 3) {
                std::wcerr << L"Usage: face_console snap <out.bmp> [--cam WxH]\n";
                return 1;
            }
            if (!ParseOpts(argc, argv, 3, opts)) return 1;
            return RunSnap(argv[2], opts);
        }
        if (cmd == L"enroll") {
            if (argc < 3) {
                std::wcerr << L"Usage: face_console enroll <ascii-name> [--cam WxH] [--det 0.5]\n";
                return 1;
            }
            if (!ParseOpts(argc, argv, 3, opts)) return 1;
            return RunEnroll(NarrowAscii(argv[2]), opts);
        }

        PrintUsage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 10;
    }
}
