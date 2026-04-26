# Checks the face-recognition model files expected under models/.
# Run from repo root: powershell -ExecutionPolicy Bypass -File scripts/download_models.ps1
#
# Phase 0 placeholder: URLs are pinned to upstream once a Release tag exists.
# For now this script enumerates expected files and warns if missing.

$ErrorActionPreference = 'Stop'

$root   = Resolve-Path (Join-Path $PSScriptRoot '..')
$models = Join-Path $root 'models'

$expected = @(
    'yunet.onnx',
    'arcface_mbf.onnx',
    '3ddfa_v2.onnx',
    'silent_face_anti_spoof.onnx',
    '3ddfa_param_mean_std.pkl'
)

New-Item -ItemType Directory -Force -Path $models | Out-Null

foreach ($name in $expected) {
    $path = Join-Path $models $name
    if (Test-Path $path) {
        Write-Host "[ok]      $name"
    } else {
        Write-Host "[missing] $name  -- drop the file into $models"
    }
}

Write-Host ""
Write-Host "Phase 4 will replace this with verified GitHub Release downloads."
