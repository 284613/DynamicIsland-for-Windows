# Downloads ONNX Runtime CPU x64 1.17.3 and unpacks into third_party/onnxruntime/
# Run from repo root: powershell -ExecutionPolicy Bypass -File scripts/setup_onnxruntime.ps1

$ErrorActionPreference = 'Stop'

$version = '1.17.3'
$pkg     = "onnxruntime-win-x64-$version"
$url     = "https://github.com/microsoft/onnxruntime/releases/download/v$version/$pkg.zip"
$root    = Resolve-Path (Join-Path $PSScriptRoot '..')
$dst     = Join-Path $root 'third_party\onnxruntime'
$tmp     = Join-Path $env:TEMP "$pkg.zip"
$extract = Join-Path $env:TEMP 'ort_extract'

Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing

if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive -Path $tmp -DestinationPath $extract -Force

$src = Join-Path $extract $pkg
New-Item -ItemType Directory -Force -Path (Join-Path $dst 'include') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $dst 'lib')     | Out-Null
Copy-Item -Recurse -Force (Join-Path $src 'include\*') (Join-Path $dst 'include')
Copy-Item -Recurse -Force (Join-Path $src 'lib\*')     (Join-Path $dst 'lib')

Remove-Item $tmp -Force
Remove-Item $extract -Recurse -Force

Write-Host "ONNX Runtime $version installed to $dst"
