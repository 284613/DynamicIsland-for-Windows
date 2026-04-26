# Face Unlock — Implementation Plan & Progress

Resumable record of the Windows lock-screen face-unlock feature. Read this first
in any new session before continuing the work.

## Goal

- **Real Windows lock-screen unlock** via a custom Credential Provider DLL.
- **Dynamic Island animations** (Face ID-style scanning / success / failed)
  shown on the desktop **after** unlock — the island cannot draw on the
  Winlogon desktop, so during scan only the CP UI is visible.
- **Autostart**: ordinary `HKCU\...\Run` entry for the main app. The CP DLL is
  separately registered under HKLM and loaded by `LogonUI.exe`.

## Security tier (selected: B)

5-layer pipeline:

1. **YuNet** detection (bbox + 5 landmarks)
2. **3DDFA_V2** 3D landmarks + head pose
3. **Active liveness** challenge (random blink / left turn / right turn,
   verified via parallax of 3D landmarks across consecutive frames)
4. **Silent-Face** RGB anti-spoofing (texture)
5. **ArcFace MobileFaceNet** identity match (cosine ≥ 0.42, must hold for 3
   consecutive frames)

5-second timeout. Enrollment captures 6 templates (front/left/right × 2).

## Hard architectural constraint

The DynamicIsland main process (user-session app) is frozen behind the lock
screen. The CP DLL paints scan UI itself; the island only plays a "welcome
back / Face ID success" animation right after unlock. This matches iPhone
behavior — the lock-screen scan UI is owned by the system, the island just
celebrates.

## Solution layout

```
DynamicIsland.sln
├── DynamicIsland           main app (existing, +autostart, +face UI)
├── face_core      [NEW]    static lib (camera + ORT + 5 layers + DPAPI store)
├── FaceUnlockProvider [NEW]  Credential Provider DLL (loads face_core)
├── tools/face_console [TODO] standalone test EXE for Phase 1
└── third_party/onnxruntime + models/   (gitignored binaries)
```

## Pinned IDs

- **face_core proj GUID**:           `{8F3A7B12-9E4C-4D5A-B6E1-1C2D3E4F5A6B}`
- **FaceUnlockProvider proj GUID**:  `{D2F8C4A1-3B5E-4F7C-9A0D-8E1F2A3B4C5D}`
- **Credential Provider CLSID**:     `{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}`
  (registered under
  `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\`)

## Models (all in `models/`, gitignored)

| File                          | Size   | Source                                                              |
|-------------------------------|--------|---------------------------------------------------------------------|
| `yunet.onnx`                  | 227 K  | OpenCV Zoo `face_detection_yunet_2023mar` (640×640 fixed input)     |
| `arcface_mbf.onnx`            | 13.0 M | InsightFace `buffalo_s` → `w600k_mbf` (112×112 BGR, [-1,1])         |
| `3ddfa_v2.onnx`               | 12.4 M | cleardusk/3DDFA_V2 `mb1_120x120` (120×120, 62-d param output)       |
| `silent_face_anti_spoof.onnx` | 1.7 M  | minivision-ai `2.7_80x80_MiniFASNetV2` (80×80 BGR)                  |
| `3ddfa_param_mean_std.pkl`    | 713 B  | 62-d param denormalization stats                                    |

**Note on 3DDFA**: full landmark reconstruction needs `bfm_noneck_v3_slim.pkl`
(BFM basis). For active-liveness we plan to use only the first 12 of 62 dims
(pose + scale + translation) → derive yaw/pitch/roll directly. Full landmarks
TBD if needed.

## ONNX I/O reference

- **YuNet**: input `[1,3,640,640]` BGR float; outputs at strides 8/16/32:
  `cls_*` `obj_*` `bbox_*` `kps_*` (12 outputs total). Decode:
  `cx=(col+bbox[0])*s; cy=(row+bbox[1])*s; w=exp(bbox[2])*s; h=exp(bbox[3])*s;`
  keypoints `(kps[2k]+col)*s, (kps[2k+1]+row)*s`. Score = `sigmoid(cls)*sigmoid(obj)`.
- **ArcFace**: input `input.1 [N,3,112,112]` BGR `(x-127.5)/127.5`, output `[N,512]`.
- **3DDFA_V2**: input `[1,3,120,120]` BGR `(x-127.5)/128`, output `[1,62]` (denormalize via pkl).
- **Silent-Face**: input `[1,3,80,80]`, output 3-class logits → softmax. Class 1 = real.

## Progress

### ✅ Phase 0 — Scaffolding (committed-ready)

- `face_core/` static lib project, x64 Debug+Release, links ORT headers.
- `FaceUnlockProvider/` DLL project, exports `DllGetClassObject` etc (stubs).
- `third_party/onnxruntime/` populated by `scripts/setup_onnxruntime.ps1`
  (ORT 1.17.3 CPU x64).
- `models/` populated (4 ONNX + 1 pkl, all derived in temp/cleaned).
- `DynamicIsland.sln` updated; whole-solution Debug x64 build verified ✅.
- `.gitignore` ignores ORT SDK + `*.onnx` + new build dirs.

### ✅ Phase 1 — face_core (done, end-to-end verified)

**End-user smoke test passed** (2026-04-26): YuNet detection + Silent-Face
liveness + 3DDFA pose + active turn-right challenge + ArcFace identity match,
all on a 640×480 NV12 UVC webcam. Final cosine similarity 0.818, well above
the 0.42 threshold.

**Bugs found & fixed during smoke testing:**

1. **Camera open failed (`hr=0xC00D36B4` MF_E_INVALIDMEDIATYPE)** — the UVC
   driver only exposes YUY2 / NV12 native subtypes, not RGB32. Fix:
   enumerate `IMFSourceReader::GetNativeMediaType`, score by subtype + size
   match, set the chosen native type, convert YUY2/NV12 → BGR in-process.
2. **Detector returned 3000+ false positives at 0.10 threshold, no real
   detections at 0.60** — YuNet 2023mar has Sigmoid baked into the cls/obj
   heads at export time. We were double-sigmoid'ing. Fix: drop our sigmoid,
   use `score = cls[idx] * obj[idx]` directly.
3. **Active liveness reaching 5 s timeout on slow webcams** — pipeline
   still passed eventually but UX was tight. Documented as a Phase 4 polish
   item: bump timeout to 10 s and/or lower yaw threshold to 9°.
4. **Production parameters chosen via cross-resolution probe** (4 verify
   runs at ~1 m): identity scores ranged 0.47–0.63 across runs. **Defaults
   pinned: `--cam 800x600 --det 0.6 --match 0.45`**. 720p offered no gain
   at this distance and cost ~2× inference; 480p had slightly more false
   positives at low threshold. Phase 2 main app will use these same values.

5. **Active-liveness redesigned per user feedback** — head-turn challenge
   was annoying for daily use. Final policy: **B-tier on enrollment,
   A-tier on verification.**
   - **Enrollment** still captures 6 templates (front/left/right × 2),
     each gated by Silent-Face + 3DDFA pose check. The multi-angle
     coverage means verification can match any natural head angle.
   - **Verification** runs Silent-Face passive liveness + ArcFace match
     only. No turn required — "look at the camera, you're in" UX.
   - **`FacePipeline::Config::requireActiveLiveness` default flipped to
     `false`**. Add `--strict` flag (or set field to `true`) to bring B-tier
     back for high-security flows (Phase 3 CP DLL may opt in here).
   - Silent-Face alone defeats casual photo / screen-replay attacks
     (~85–95% effective). True 3D anti-spoofing requires IR hardware
     (Windows Hello territory) — out of scope without a structured-light
     camera. Documented limitation.

## Final Phase 1 production parameters

These ship as defaults in `face_console`, `FacePipeline::Config`, and (via
Phase 2 wiring) the main app + Credential Provider:

| Setting                       | Value      | Rationale                                |
|-------------------------------|------------|------------------------------------------|
| Camera resolution             | 800×600    | Best probe results, low CPU cost         |
| YuNet detector threshold      | 0.6        | Stable detect@0.6 across tested distance |
| ArcFace match threshold       | 0.45       | 0.05 margin above worst observed score   |
| Silent-Face real threshold    | 0.70       | Upstream-recommended baseline            |
| Consecutive matches required  | 3          | ~100 ms confirmation window at 30 fps    |
| Active liveness (verification)| **off**    | UX: "look-and-unlock" like Face ID       |
| Active liveness (enrollment)  | **on**     | Multi-angle templates, anti-spoof during enroll |



**Done:**
- `Image.h/cpp` — Image struct, ResizeBilinear, WarpAffineBilinear, NV12→BGR.
- `ModelLoader.h/cpp` — shared `Ort::Env`, model path resolver
  (exe-dir/models → walks up → `%LOCALAPPDATA%\DynamicIsland\models`),
  `OrtSession` wrapper caching input/output names.
- `FaceDetector.h/cpp` — full YuNet decode (3 strides + sigmoid score + NMS).
- `FaceAligner.h/cpp` — 5-point Umeyama similarity transform → inverse 2×3
  affine; `AlignArcFace` returns 112×112 crop.
- `FaceRecognizer.h/cpp` — ArcFace embedding with L2 norm,
  `CosineSimilarity` static, `kDefaultMatchThreshold = 0.42`.
- `FaceTemplateStore.h/cpp` — DPAPI-encrypted store at
  `%LOCALAPPDATA%\DynamicIsland\faces.bin`, magic+version header,
  `Add/Remove/ListNames/Match/Save/Load`.
- `CameraCapture.h/cpp` — Media Foundation source reader, default device,
  RGB32 → BGR24 frames, blocking `ReadFrame()`.
- `face_core.vcxproj` updated with all 8 source files.
- **Build verified**: `face_core.lib` produces clean (1 narrowing warning fixed).

**Remaining for Phase 1:**

1. ✅ `LivenessDetector.h/cpp` — Silent-Face 80×80, 3-class softmax → class-1
   probability. Crop uses upstream's `2.7x` recipe (clipped inside bounds).
2. ✅ `FaceLandmarks3D.h/cpp` — 3DDFA 120×120, 62-d output. `LoadParamStats`
   parses the upstream `.pkl` directly (BINUNICODE + SHORT_BINBYTES tag scan,
   no Python deps). `ExtractPose` decomposes rotation matrix to yaw/pitch/roll
   with proper gimbal-lock handling.
3. ✅ `ActiveLiveness.h/cpp` — head-pose challenge (TurnLeft / TurnRight),
   yaw threshold ±12°, return-to-center within 4°, 5 s timeout. RNG seeded
   per session so the challenge picks randomly.
4. ✅ `FacePipeline.h/cpp` — single-call `OnFrame()` orchestrator
   (`Idle → Detecting → Verifying → Challenging → Identifying → Success/Failed`).
   Requires N consecutive matches (default 3), per-stage hints emitted via
   callback.
5. ✅ `tools/face_console/` — test EXE with subcommands
   `selftest | list | enroll <name> | verify | clear`.
   Project links `onnxruntime.lib` + Crypt32 + MF; post-build copies
   `onnxruntime.dll` and `models/` next to `$(OutDir)`.
6. ✅ **Build smoke test**: `face_console.exe selftest` loads all 4 ONNX
   sessions and reports per-component status. Verified output:
   ```
   [selftest] detector output count=0
   [selftest] silent-face real=0.008
   [selftest] 3ddfa=ok yaw=21.97 pitch=0.41 roll=5.15
   [selftest] arcface=ok
   [selftest] store=ok templates=0
   ```
7. ⏳ **End-user smoke test** (user runs manually):
   ```powershell
   cd E:\vs\c++\DynamicIsland\x64\Debug
   .\face_console.exe enroll <yourname>     # 6 captures (front/left/right × 2)
   .\face_console.exe list                  # confirms templates persisted
   .\face_console.exe verify                # continuous match loop
   ```
   Photo-attack check: hold a printed/displayed photo of the enrolled face
   to the camera; pipeline should fail with `Passive liveness failed`.

### ⏳ Phase 2 — Main app integration

- `src/AutoStartManager.cpp` — read/write `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\DynamicIsland`.
- `src/FaceEnrollWindow.cpp` — Direct2D-drawn 3-step wizard
  (front/left/right, 2 captures each).
- `src/components/FaceIdComponent.cpp` — three states:
  - **Scanning**: narrow capsule, rotating scan ring (Face ID-like)
  - **Success**: green checkmark + pulse, 1 s
  - **Failed**: red X shake, 1.5 s
- `src/DynamicIsland.cpp` — add mode `ModeFaceUnlockFeedback` (highest priority).
- `src/FaceUnlockBridge.cpp` — listen on named pipe
  `\\.\pipe\DynamicIsland.FaceUnlock`, dispatch events to component.
- `src/SettingsWindow.cpp` — add "人脸解锁" group: enable toggle, enroll button,
  delete button, autostart toggle.
- Enrollment flow includes a one-time Windows password prompt → DPAPI-encrypted
  into the same `faces.bin` (or a sibling file) for CP DLL to use later.

### ⏳ Phase 3 — Credential Provider DLL

- DLL skeleton: `IClassFactory`, `DllGetClassObject`, `DllRegisterServer`,
  `DllUnregisterServer`. INITGUID lives in dllmain.cpp.
- `CFaceCredentialProvider` (`ICredentialProvider`):
  - Respond only to `CPUS_LOGON` and `CPUS_UNLOCK_WORKSTATION`.
  - One credential per user.
- `CFaceCredential` (`ICredentialProviderCredential2`):
  - Fields: tile image (state indicator) + status text + cancel link +
    hidden password fallback box.
  - `SetSelected` triggers `UnlockOrchestrator`.
  - `GetSerialization` builds `KERB_INTERACTIVE_UNLOCK_LOGON` from
    DPAPI-decrypted password and identity-match name.
- `UnlockOrchestrator`: runs `FacePipeline` on a worker thread, pumps state
  text via `events->SetFieldString`, fires `events->CredentialsChanged` on
  success.
- `IpcNotifier`: short-lived named-pipe write `{event, user}` (consumed by
  the main app once user-session resumes after unlock).
- Registration via `regsvr32 FaceUnlockProvider.dll` invoked from the
  settings page enable toggle. **Note**: needs admin elevation.

### ⏳ Phase 4 — End-to-end polish

- Lock → CP UI → recognition → unlock → island success animation.
- Failure fallback: liveness/threshold fail → CP shows "请输入密码".
- Edge cases: camera busy/missing → CP collapses gracefully; settings page
  disables enable toggle when no camera detected.
- Performance: ≥ 5 fps on integrated CPU; total recognition < 2 s.
- Optional "performance mode" toggle that skips 3DDFA (degrades to A-tier).
- `scripts/download_models.ps1` updated to fetch from a GitHub Release tag
  with SHA-256 verification.

## Build commands

```powershell
# Whole solution
& "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  DynamicIsland.sln /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo

# Single project
& "<msbuild>" face_core\face_core.vcxproj /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
```

User's MSBuild path is `D:\Program Files\...` (not C:). VS 2022 v143
toolset, C++17, Unicode, static CRT (`/NODEFAULTLIB:MSVCRT` on the main app).

## Key project decisions / nuances

1. **No OpenCV dependency** — implemented YuNet decode, similarity transform,
   bilinear resize/warp, NV12→BGR, NMS by hand. Saves ~80 MB of DLLs.
2. **DPAPI scope**: `Save()` first tries `LOCAL_MACHINE` (so the CP DLL,
   running under SYSTEM during lock, can decrypt). Falls back to user-scope
   if that fails. **TODO Phase 3**: confirm SYSTEM can decrypt the
   user-scoped blob — if not, the CP needs a different storage strategy
   (e.g., LSA secret or HKLM-protected blob).
3. **Camera in CameraCapture** uses `RGB32` output mode (alpha-stripped to
   BGR24). Top-down vs bottom-up stride is honored.
4. **YuNet kp order** (right-eye, left-eye, nose, right-mouth, left-mouth
   from subject's POV) matches ArcFace's reference template, so direct
   similarity transform works.
5. **3DDFA pose extraction**: from the 62-d output, dims [0..11] hold
   `s*R(3x3)` flattened plus translation. After denormalization, decompose
   to derive yaw/pitch/roll without needing the BFM basis.
6. **Phase 1 active-liveness MVP** uses head-pose delta only (saves shipping
   the BFM blob). Eye-blink via 2D YuNet landmarks is a follow-up.

## Next session — start here

Phase 1 is closed. Pick up Phase 2 (main app integration). Order:

1. `src/AutoStartManager.cpp` — read/write
   `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\DynamicIsland`.
2. `src/FaceEnrollWindow.cpp` — Direct2D 3-step wizard (front/left/right
   ×2 each). Reuse the enrollment loop from
   `tools/face_console/main.cpp::RunEnroll` — same logic, just D2D
   rendering instead of console output. Active liveness ON during enroll.
3. `src/components/FaceIdComponent.cpp` — three island states:
   Scanning (rotating arc), Success (green check + pulse, 1 s),
   Failed (red X shake, 1.5 s). All inside the existing island geometry.
4. `src/DynamicIsland.cpp` — add mode `ModeFaceUnlockFeedback`, top
   priority, auto-dismiss after the visual finishes.
5. `src/FaceUnlockBridge.cpp` — listen on
   `\\.\pipe\DynamicIsland.FaceUnlock`, dispatch JSON events to the
   FaceIdComponent (this is where Phase 3's CP DLL will publish events
   after lock-screen unlock).
6. `src/SettingsWindow.cpp` — new "人脸解锁" group:
   - 启用人脸解锁 (toggle, no-op until Phase 3)
   - 录入人脸… (opens FaceEnrollWindow)
   - 删除已录入
   - 开机自启动 (AutoStartManager)
   - 性能模式 / 跳过 3DDFA(stays off by default)

Phase 2 success looks like: open settings → click "录入人脸" → enroll
through the wizard → see the island flash a Face ID success animation.
No lock-screen integration yet (that's Phase 3).

Resume in any new session with:
```
读 docs/FaceUnlockPlan.md 继续 Phase 2
```

---

## Phase 1 — Detailed module specs

These are the sketches I want to hold to. Header-first, then implementation.

### `LivenessDetector` (Silent-Face)

```cpp
// face_core/include/face_core/LivenessDetector.h
class LivenessDetector {
public:
    LivenessDetector();
    // Run on the full BGR frame; bbox is the YuNet box, expanded by ~30% so
    // Silent-Face sees forehead and chin. Returns probability in [0,1] that
    // the face is real (class 1 of the 3-class softmax).
    float Score(const Image& bgr, const FaceBox& box);

    static constexpr float kDefaultRealThreshold = 0.70f;
private:
    OrtSession session_;  // model: silent_face_anti_spoof.onnx
};
```

Preprocessing (MiniFASNetV2 convention):
1. `cx = box.x + box.w/2; cy = box.y + box.h/2`
2. `side = max(box.w, box.h) * 1.30f`  (their crop scale = 2.7 → 1.30 stays
   inside the safe range; tune later if needed)
3. Square crop centered on `(cx, cy)` with side `side`, clipped to image bounds.
4. Resize to 80×80, BGR uint8 → `chw float / 255.0` (no mean subtraction —
   the model has BN; verify by re-reading `MiniFASNet.py` `forward`).
5. Run, take `softmax(logits)[1]` as the real-face probability.

### `FaceLandmarks3D` (3DDFA_V2)

```cpp
// face_core/include/face_core/FaceLandmarks3D.h
struct HeadPose {
    float yaw;    // degrees, positive = right
    float pitch;  // positive = down
    float roll;   // positive = ccw on screen
};

class FaceLandmarks3D {
public:
    FaceLandmarks3D();
    // Returns the 62-d denormalized parameter vector. Caller decides what
    // to do with it (we only use the pose head in B-tier MVP).
    bool Run(const Image& bgr, const FaceBox& box, std::array<float, 62>& outParams);

    // Decode head pose from the first 12 params. The 12 are:
    //   p[0..8]  = scale * R (3x3, row-major)
    //   p[9..11] = translation (tx, ty, tz)
    // After QR-style normalization of the upper 3x3, derive Euler angles.
    static HeadPose ExtractPose(const std::array<float, 62>& params);

private:
    OrtSession session_;
    std::array<float, 62> mean_;
    std::array<float, 62> std_;
    void LoadParamStats();   // reads models/3ddfa_param_mean_std.pkl
};
```

Preprocessing: like Silent-Face — expand box, square-crop, resize to 120×120.
The model expects `(x - 127.5) / 128`, BGR.

**Pose decode** (single source of truth — copy into the impl, don't redo):

```
R_raw = reshape(p[0..8], 3, 3)    // row-major
s     = norm(R_raw[:,0])          // scale = column norm
R     = R_raw / s                 // pure rotation, near-orthogonal
yaw   = degrees(asin( -R[2,0] ))
pitch = degrees(atan2( R[2,1], R[2,2] ))
roll  = degrees(atan2( R[1,0], R[0,0] ))
```

Numerical safety: clamp the asin argument to [-1, 1].

`3ddfa_param_mean_std.pkl` is a pickle of a dict with keys `mean` and `std`.
Phase 1 reads it once at construction. We can avoid bringing in `pickle` by
re-saving as raw little-endian binary at build time — but it's only 713 B
and we read it once, so a tiny hand-rolled pickle subset parser is fine
(version 4 protocol with NUMPY array dump). **Easier**: regenerate as a
plain `.bin` (124 floats) via a helper Python one-liner during model setup.

### `ActiveLiveness`

```cpp
// face_core/include/face_core/ActiveLiveness.h
enum class Challenge : uint8_t { TurnLeft = 0, TurnRight = 1 };

enum class ActiveState : uint8_t {
    Idle, Detect, Challenge, Verify, Pass, FailTimeout, FailSpoof
};

class ActiveLiveness {
public:
    void Start(uint32_t seed);                // randomize the challenge
    void Reset();
    Challenge CurrentChallenge() const;
    ActiveState State() const;

    // Feed each frame's pose. The state machine decides progress.
    // Returns the latest state.
    ActiveState OnFrame(const HeadPose& pose, double timestampSeconds);

    // Hint string suitable for UI overlay ("请向左轻微转头" / "请向右转头" / ...).
    const wchar_t* Hint() const;

    static constexpr float kYawThresholdDeg = 12.0f;     // delta required
    static constexpr double kTimeoutSeconds = 5.0;
};
```

Algorithm (MVP):
1. `Start()` records the initial pose at t0 and picks a random challenge.
2. On each `OnFrame`:
   - If `t - t0 > 5s` → `FailTimeout`.
   - Compute `dyaw = pose.yaw - initialYaw`.
   - For `TurnLeft` (yaw decreases on most cameras — flip if mirrored),
     wait for `dyaw <= -kYawThresholdDeg` then return-toward-zero
     (`|dyaw| < 4°`) → `Pass`.
   - For `TurnRight` mirror.
3. Eye-blink challenge can be added later; needs the YuNet 5 landmarks and
   an EAR (eye aspect ratio) calculation, but that's only 2 landmarks for
   inner eye corners — not enough. Either ship a small dlib 68-pt model or
   skip blink for now.

### `FacePipeline`

```cpp
// face_core/include/face_core/FacePipeline.h
enum class PipelineStage : uint8_t {
    Idle, Detecting, Challenging, Verifying, Identifying, Success, Failed
};

struct PipelineEvent {
    PipelineStage stage;
    std::wstring hint;          // for UI ("请眨眼" / "识别中...")
    float score = 0;            // best identity cosine
    std::string identity;       // matched name on Success
};

using PipelineCallback = std::function<void(const PipelineEvent&)>;

class FacePipeline {
public:
    struct Config {
        float matchThreshold      = 0.42f;
        float realThreshold       = 0.70f;
        int   consecutiveMatches  = 3;     // identity must hold N frames
        bool  requireActiveLiveness = true;
        bool  requireSilentFace     = true;
        uint32_t challengeSeed    = 0;     // 0 = clock-based
    };

    FacePipeline(std::shared_ptr<FaceTemplateStore> store, const Config& cfg);

    // Drive one frame through the pipeline. Returns true while still running.
    // Emits intermediate state via the callback. Once Success/Failed is
    // emitted, further calls return false.
    bool OnFrame(const Image& bgr, double timestamp, PipelineCallback cb);

    void Reset();
};
```

The pipeline **does not own the camera**. Caller pumps frames; this lets the
console demo, the enroll wizard, and the CP DLL all share one implementation.

### `tools/face_console`

Minimal `int wmain(int argc, wchar_t** argv)`:

```
face_console enroll <name>
    Opens the camera, runs the pipeline in a loop, captures 6 templates
    (front/left/right × 2). For each template, requires a passing
    SilentFace + ArcFace pass and the desired pose (front: |yaw|<5°,
    left: yaw<-15°, right: yaw>15°).

face_console verify
    Continuous loop. Each frame: pipeline → print
    `[stage] hint score=... identity=...`. Press Ctrl+C to exit.

face_console list
    Prints all stored template names + counts.

face_console clear
    Wipes the store. Confirmation prompt.
```

Project file: `tools/face_console/face_console.vcxproj`, ConfigurationType
`Application`, SubSystem `Console`. References `face_core.vcxproj`. Links
`onnxruntime.lib` from `..\..\third_party\onnxruntime\lib`. Post-build copy
of `onnxruntime.dll` and `models/*` into `$(OutDir)`.

---

## Phase 2 — Detailed UI specs

### `FaceIdComponent` rendering states

All states draw inside the existing island geometry (no new HWND).
LayoutController gets a new "compact-action" mode that resizes the island
to a 240×52 capsule for these states.

| State        | Visual                                                                  | Duration |
|--------------|-------------------------------------------------------------------------|----------|
| `Scanning`   | Indigo background; rotating 3-arc ring (240° gap, 60 deg/s); subtle    | until stopped |
|              | downward chevron on the right hinting "scanning ↓"                       |          |
| `Success`    | Background eases to green-50; ring snaps to a checkmark stroke that    | 1.0 s   |
|              | draws in 200 ms; gentle radial pulse                                     |          |
| `Failed`     | Background red-50; ring becomes an X; 6 px horizontal shake, 80 ms     | 1.5 s   |
|              | period × 3                                                                |          |

After `Success`, fade back to whatever previous mode the island was in.
`Failed` likewise, but optionally shows a small "请输入密码" hint below the
island for 1.0 s before dismiss.

### Settings page layout (`SettingsWindow.cpp`)

New section "人脸解锁" between the existing Pomodoro and Agent groups:

```
[ 人脸解锁 ]
  ☐ 启用人脸解锁
        (toggling on triggers admin-elevated regsvr32 of the CP DLL;
         off → unregister)
  [录入人脸…]   →  opens FaceEnrollWindow
  [删除已录入]  →  clears faces.bin (with confirmation)
  状态: 已录入 6 个模板 | 未录入

  ☐ 开机自启动
        (writes/clears HKCU\Software\Microsoft\Windows\CurrentVersion\
         Run\DynamicIsland)

  ☐ 性能模式 (跳过 3DDFA)
        (degrades to A-tier; recommended only on low-end CPUs)
```

### Enrollment flow (`FaceEnrollWindow.cpp`)

Direct2D-rendered modal owned by the main app HWND.

1. Welcome screen: "请将摄像头对准面部" + camera preview pane (640×480 D2D
   bitmap, updated from a worker thread that pumps `CameraCapture`).
2. Three guided steps. Each step displays its required pose in an arrow:
   - 正面 (target pose: |yaw|<5°, |pitch|<10°)
   - 微微向左 (target: yaw < -12°)
   - 微微向右 (target: yaw > 12°)
   For each step, the wizard waits for the pose to hold for ~500 ms with
   liveness passing, then captures **2 embeddings** spaced 250 ms apart.
3. Password prompt:
   - "请输入您的 Windows 密码以完成录入"
   - Stores DPAPI-encrypted blob at
     `%LOCALAPPDATA%\DynamicIsland\unlock_secret.bin` (separate from
     faces.bin so we can rotate independently).
4. Done screen with summary and "完成" button.

### Pipe protocol (main app ↔ CP DLL)

Pipe name: `\\.\pipe\DynamicIsland.FaceUnlock`
Direction: **CP writes, main app reads**. One-shot per unlock event.

JSON, one line, UTF-8:

```json
{ "event": "scan_started" }                         (optional)
{ "event": "success", "user": "ihfg27951" }
{ "event": "failed",  "reason": "timeout" | "spoof" | "no_match" }
```

Main app's `FaceUnlockBridge` runs a worker thread that:
- Creates the pipe with `PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE`.
- ACL: only allow the local SYSTEM account + the current interactive user.
- On read, posts a custom message to the island HWND so rendering happens
  on the UI thread.

### Autostart manager (`AutoStartManager.cpp`)

Trivially:

```cpp
namespace AutoStart {
    bool IsEnabled();          // reads HKCU\...\Run\DynamicIsland
    bool SetEnabled(bool on);  // writes/deletes; returns success
    std::wstring CommandLine();// "<exe> --autostart" so we can detect bootup runs
}
```

Main app on `--autostart`: skip the welcome animation, jump straight to
compact mode (faster perceived boot).

---

## Phase 3 — Credential Provider details

### Registry registration (`DllRegisterServer`)

Three keys must exist after registration:

```
HKEY_CLASSES_ROOT\CLSID\{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}
   (default)                  = "DynamicIsland Face Credential Provider"
  HKEY_CLASSES_ROOT\CLSID\{7B3C9F2E-...}\InprocServer32
   (default)                  = "<full path to FaceUnlockProvider.dll>"
   ThreadingModel             = "Apartment"

HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\
  Credential Providers\{7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}
   (default)                  = "DynamicIsland Face Credential Provider"
```

Optional filter registration if we need to **hide other CPs** (we don't, so
skip — coexist with the default password CP as fallback).

`DllUnregisterServer` deletes all three.

### CP scenarios handled

- `CPUS_LOGON` (cold boot login)
- `CPUS_UNLOCK_WORKSTATION` (Win+L resume)

NOT handled:
- `CPUS_CREDUI` — third-party UAC prompts. Return `E_NOTIMPL`.
- `CPUS_CHANGE_PASSWORD` — Return `E_NOTIMPL`.

### Credential serialization

Build `KERB_INTERACTIVE_UNLOCK_LOGON`:

```cpp
KERB_INTERACTIVE_UNLOCK_LOGON kiul = {};
kiul.Logon.MessageType = KerbInteractiveLogon;
// LogonDomainName: machine name for local accounts ("." won't work here).
// UserName: the matched identity from the face pipeline.
// Password: DPAPI-decrypted, then wrapped via KerbInteractiveUnlockLogonInit
//          (this rotates the password buffer with the LSA pack key).
KerbInteractiveUnlockLogonInit(domain, username, password, usage, &kiul);
KerbInteractiveUnlockLogonPack(kiul, &serialized, &serializedSize);
*pcpcs->rgbSerialization = serialized;
*pcpcs->cbSerialization  = serializedSize;
pcpcs->ulAuthenticationPackage = ...;  // NEGOSSP_NAME_A or KERB_NEGOTIATE
pcpcs->clsidCredentialProvider = CLSID_FaceCredentialProvider;
```

The Microsoft SampleAllControlsCredentialProvider in
`microsoft/Windows-classic-samples` has a working
`KerbInteractiveUnlockLogonInit` helper — copy it verbatim, attribution in
header.

### DPAPI under SYSTEM / locked desktop

**The unresolved question**: when LogonUI loads our DLL during
`CPUS_UNLOCK_WORKSTATION`, the DLL runs as SYSTEM (not the user). DPAPI
blobs encrypted under the user's LOCAL_USER scope cannot be decrypted by
SYSTEM. Two strategies, decide in early Phase 3:

**A. CRYPTPROTECT_LOCAL_MACHINE scope** (current Phase 1 default)
- Encrypted with a machine-scoped key (HKLM\SECURITY\Policy\Secrets\... DPAPI).
- Any process on the machine can decrypt — including any user process.
- Trade-off: another local user could in principle decrypt your password.
  Acceptable on personal devices, not on shared workstations.

**B. LSA secret**
- Use `LsaStorePrivateData` from the user-side enrollment with a
  **SYSTEM-only** ACL applied via the secret name pattern (`L$...`).
- Read with `LsaRetrievePrivateData` from the CP.
- Pros: only SYSTEM can read.
- Cons: requires elevation at enrollment time (we already need it for
  regsvr32, so no extra UX cost). More complex, but the right answer.

**Plan**: ship **A** in Phase 3 MVP, migrate to **B** in Phase 4 polish.
Document in user-facing settings: "本设备的人脸解锁密码以本机密钥加密存储,
共享电脑请勿启用".

### CP UI animation approximation

Credential Providers can only declare predefined fields via `GetFieldDescriptor`:

- `CPFT_SMALL_TEXT`, `CPFT_LARGE_TEXT`, `CPFT_COMMAND_LINK`,
  `CPFT_PASSWORD_TEXT`, `CPFT_TILE_IMAGE`, `CPFT_CHECKBOX`, `CPFT_SUBMIT_BUTTON`.

No custom drawing. To animate the scan ring:

- Use `CPFT_TILE_IMAGE` + `events->SetFieldBitmap` called every ~150 ms from
  the orchestrator thread with one of N pre-rendered frames (rotating arc,
  10–12 frames). Bitmap creation: GDI+ at startup, kept in a flyweight cache.
- Fallback: if `SetFieldBitmap` rate-limits, simply update text "扫描中..." /
  "请向左转头" — less pretty but functional.

### Driver / strategy when no camera available

`CFaceCredentialProvider::SetUsageScenario` checks for a video device via
`MFEnumDeviceSources` (cheap, ~10 ms). If none, return
`CredentialProviderCount = 0` so the password CP shows alone. Re-check on
each `Advise/UnAdvise` cycle.

---

## Phase 4 — End-to-end checklist

1. Cold-boot login flow: power on → password? Or face? Confirm CP shows up.
2. `Win+L` lock → CP UI → blink/turn challenge → unlock. Time the full path.
3. `Win+L` lock → cancel → password fallback works.
4. `Win+L` lock → unplug camera mid-scan → graceful fallback to password.
5. Camera in use by another app (Teams) → CP collapses to password-only.
6. Photo attack: print enrollment selfie → liveness should reject.
7. Screen replay: hold up phone with own face video → liveness should reject.
8. Twin / similar-face attack: not in scope, log expected behavior.
9. Multiple users on the same machine: only the enrolling user's CP shows
   (filter by SID, not implemented in MVP — file as Phase 4 follow-up).
10. Performance: log per-frame timings; goal ≥ 5 fps on integrated CPU.
11. Memory: track DLL working set; goal < 80 MB while idle, < 150 MB scanning.
12. Uninstall flow: settings toggle off → regsvr32 /u → reboot → CP gone.

---

## Risk register (full)

| # | Risk                                                            | Severity | Mitigation                                                  |
|---|------------------------------------------------------------------|----------|-------------------------------------------------------------|
| 1 | CP DLL fails to load due to unsigned binary                      | High     | Test early; if blocked, document signing requirement        |
| 2 | DPAPI/SYSTEM mismatch breaks unlock                              | High     | Strategy A in MVP, plan B for Phase 4                       |
| 3 | YuNet decode mismatch produces wrong bbox                        | Medium   | Smoke test + visual inspection in console demo              |
| 4 | ArcFace threshold 0.42 too loose for impostors                   | Medium   | Add per-user calibration in Phase 4                         |
| 5 | Silent-Face fooled by high-res print                             | Medium   | Active liveness layer covers this; document residual risk   |
| 6 | 3DDFA pose extraction wrong sign / axis convention               | Medium   | Print yaw/pitch/roll in console demo first                  |
| 7 | Camera enumeration fails on some laptops (Realtek, etc.)         | Medium   | Try alternate device index; show clear error in settings    |
| 8 | LogonUI does not refresh credential text on `CredentialsChanged` | Low      | Already a known pattern; reference MSFT sample              |
| 9 | Pipe ACL too permissive — local malware can fake unlock events   | Low      | Restrict to SYSTEM + interactive user via SDDL              |
| 10| User changes Windows password after enrollment                   | High     | Phase 4: detect logon failure, prompt to re-enter password  |

---

## Testing approach

- **Unit-style**: `tools/face_test/` runs a fixture (recorded BGR frames
  from disk) through each module independently and asserts on outputs.
  Skipped in Phase 1 in interest of time; revisit if regressions appear.
- **Integration**: `face_console` is the primary integration test through
  Phase 1.
- **Manual**: Phase 4 checklist above.
- **No CI** for this feature (camera-bound), only local validation.

---

## Open questions

These need user input or research before the relevant phase:

1. **Phase 2 — admin elevation UX**: should toggling "启用人脸解锁" trigger a
   UAC prompt in-app, or pop a separate elevated helper EXE? In-app prompt
   is cleaner but requires the main app to ship a manifest with
   `requestedExecutionLevel="asInvoker"` and re-launch self with `runas`.

2. **Phase 3 — multi-user**: do we filter the CP by SID so it only shows
   for users who have enrolled? Required if the machine has multiple
   accounts; not required for the user's personal device.

3. **Phase 4 — distribution**: the CP DLL should ideally be code-signed
   to avoid Smart App Control / WDAC issues. Does the user have a code
   signing certificate? If not, document the workaround (turn off Smart
   App Control or sign with a local self-signed cert + import as trusted).

4. **License**: InsightFace is MIT, OpenCV Zoo is Apache 2.0,
   3DDFA_V2 is MIT (with non-commercial caveat on BFM data — we don't ship
   BFM in MVP), Silent-Face-Anti-Spoofing is Apache 2.0. Add a
   `THIRD_PARTY_NOTICES.md` in Phase 4 listing all four with license text.

---

## File-by-file checklist for handoff

When resuming, verify each of these exists and is wired into the build:

- [x] `face_core/include/face_core/Image.h`
- [x] `face_core/include/face_core/ModelLoader.h`
- [x] `face_core/include/face_core/FaceDetector.h`
- [x] `face_core/include/face_core/FaceAligner.h`
- [x] `face_core/include/face_core/FaceRecognizer.h`
- [x] `face_core/include/face_core/FaceTemplateStore.h`
- [x] `face_core/include/face_core/CameraCapture.h`
- [ ] `face_core/include/face_core/LivenessDetector.h`
- [ ] `face_core/include/face_core/FaceLandmarks3D.h`
- [ ] `face_core/include/face_core/ActiveLiveness.h`
- [ ] `face_core/include/face_core/FacePipeline.h`
- [x] `face_core/src/*.cpp` (matching the above)
- [ ] `tools/face_console/main.cpp`
- [ ] `tools/face_console/face_console.vcxproj`
- [ ] `DynamicIsland.sln` includes `face_console`
- [x] `models/3ddfa_param_mean_std.pkl` present (or converted to `.bin`)
- [x] `third_party/onnxruntime/lib/onnxruntime.dll` present
- [ ] `face_console.exe` runs and produces a positive match

After Phase 1 closes, copy this checklist into Phase 2 and start the next set.
