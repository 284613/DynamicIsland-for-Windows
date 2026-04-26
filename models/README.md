# Face recognition models

Five files drive the B-tier pipeline. They are **not committed** to the repo.

| File                          | Purpose                          | Size  | Source                                                              |
|-------------------------------|----------------------------------|-------|---------------------------------------------------------------------|
| `yunet.onnx`                  | Face detection + 5 landmarks     | 227K  | OpenCV Zoo `face_detection_yunet_2023mar`                           |
| `arcface_mbf.onnx`            | 512-d identity embedding         | 13.0M | InsightFace `buffalo_s` → `w600k_mbf`                               |
| `3ddfa_v2.onnx`               | 3D landmarks + head pose         | 12.4M | cleardusk/3DDFA_V2 `mb1_120x120` (exported from PyTorch, opset 13)  |
| `silent_face_anti_spoof.onnx` | RGB liveness                     | 1.7M  | minivision-ai `2.7_80x80_MiniFASNetV2` (exported, opset 13)         |
| `3ddfa_param_mean_std.pkl`    | 62-d param denormalization stats | 1K    | cleardusk/3DDFA_V2 `configs/param_mean_std_62d_120x120.pkl`         |

## Provenance / how the ONNX files were produced

- **YuNet, ArcFace**: official ONNX, downloaded as-is.
- **3DDFA_V2**: cloned upstream zip → ran `utils/onnx.convert_to_onnx` against
  `weights/mb1_120x120.pth` (62-param head, MobileNetV1 backbone, 120×120 input).
- **Silent-Face**: cloned upstream zip → exported `MiniFASNetV2(conv6_kernel=(5,5))`
  with input `1×3×80×80`, opset 13.

Both export scripts ran on PyTorch 2.5.1 + Python 3.12 with `do_constant_folding=True`.

## Runtime location

The loader prefers this directory when present (dev mode). For installed builds
the app falls back to `%LOCALAPPDATA%\DynamicIsland\models\` and downloads
on first run from a GitHub Release tag (Phase 4 wires this).
