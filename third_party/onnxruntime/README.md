# ONNX Runtime (CPU x64)

This directory holds the ONNX Runtime C++ SDK used by `face_core`.

## Layout expected by build

```
third_party/onnxruntime/
├── include/
│   ├── onnxruntime_cxx_api.h
│   ├── onnxruntime_c_api.h
│   └── ...
├── lib/
│   ├── onnxruntime.lib
│   └── onnxruntime.dll
└── README.md (this file)
```

## How to populate

Run `scripts/setup_onnxruntime.ps1` from the repo root. It downloads the
prebuilt CPU x64 release and unpacks it here.

Pinned version: **1.17.3** (CPU, x64, Windows).

The DLL must be deployed next to `DynamicIsland.exe` and `FaceUnlockProvider.dll`
at run time. The build copies it post-build (Phase 1 wires this up).
