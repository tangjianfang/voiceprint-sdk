# VoicePrint SDK Packaging Script
# Creates a distributable SDK package

param(
    [string]$OutputDir = "dist",
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$PackageName = "voiceprint-sdk-v$Version-win-x64"
$PackageDir = Join-Path $OutputDir $PackageName

Write-Host "=== VoicePrint SDK Packager ===" -ForegroundColor Cyan
Write-Host "Version: $Version"
Write-Host "Output:  $PackageDir"
Write-Host ""

# Clean previous output
if (Test-Path $PackageDir) {
    Remove-Item $PackageDir -Recurse -Force
}

# Create directory structure
$dirs = @(
    "$PackageDir/bin",
    "$PackageDir/include/voiceprint",
    "$PackageDir/lib",
    "$PackageDir/models",
    "$PackageDir/examples/cpp",
    "$PackageDir/examples/csharp",
    "$PackageDir/doc",
    "$PackageDir/reports"
)
foreach ($d in $dirs) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

Write-Host "[1/7] Copying runtime binaries..." -ForegroundColor Green
Copy-Item "build\bin\Release\voiceprint.dll" "$PackageDir\bin\"
Copy-Item "build\bin\Release\onnxruntime.dll" "$PackageDir\bin\"

Write-Host "[2/7] Copying development files..." -ForegroundColor Green
Copy-Item "include\voiceprint\voiceprint_api.h" "$PackageDir\include\voiceprint\"
Copy-Item "build\lib\Release\voiceprint.lib" "$PackageDir\lib\"

Write-Host "[3/7] Copying models..." -ForegroundColor Green
Copy-Item "models\ecapa_tdnn.onnx" "$PackageDir\models\"
Copy-Item "models\silero_vad.onnx" "$PackageDir\models\"

Write-Host "[4/7] Copying examples..." -ForegroundColor Green
Copy-Item "examples\cpp_demo\main.cpp" "$PackageDir\examples\cpp\"
Copy-Item "examples\csharp_demo\Program.cs" "$PackageDir\examples\csharp\"
Copy-Item "examples\csharp_demo\VoicePrintDemo.csproj" "$PackageDir\examples\csharp\"

Write-Host "[5/7] Copying demo executable..." -ForegroundColor Green
Copy-Item "build\bin\Release\cpp_demo.exe" "$PackageDir\bin\"

Write-Host "[6/7] Copying reports..." -ForegroundColor Green
if (Test-Path "reports\benchmark_report.txt") {
    Copy-Item "reports\benchmark_report.txt" "$PackageDir\reports\"
}
if (Test-Path "reports\evaluation_report.txt") {
    Copy-Item "reports\evaluation_report.txt" "$PackageDir\reports\"
}

Write-Host "[7/7] Generating SDK documentation..." -ForegroundColor Green
Copy-Item "doc\SDK_README.md" "$PackageDir\doc\"
Copy-Item "doc\SDK_README.md" "$PackageDir\README.md"

# Compute sizes
$dllSize = (Get-Item "$PackageDir\bin\voiceprint.dll").Length / 1MB
$ortSize = (Get-Item "$PackageDir\bin\onnxruntime.dll").Length / 1MB
$model1Size = (Get-Item "$PackageDir\models\ecapa_tdnn.onnx").Length / 1MB
$model2Size = (Get-Item "$PackageDir\models\silero_vad.onnx").Length / 1MB
$totalSize = (Get-ChildItem $PackageDir -Recurse -File | Measure-Object -Sum Length).Sum / 1MB

Write-Host ""
Write-Host "=== Package Contents ===" -ForegroundColor Cyan
Write-Host "  voiceprint.dll:    $([math]::Round($dllSize, 2)) MB"
Write-Host "  onnxruntime.dll:   $([math]::Round($ortSize, 2)) MB"
Write-Host "  ecapa_tdnn.onnx:   $([math]::Round($model1Size, 2)) MB"
Write-Host "  silero_vad.onnx:   $([math]::Round($model2Size, 2)) MB"
Write-Host "  Total:             $([math]::Round($totalSize, 2)) MB"
Write-Host ""

# Create ZIP
$zipPath = Join-Path $OutputDir "$PackageName.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $PackageDir -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = (Get-Item $zipPath).Length / 1MB
Write-Host "=== Package Created ===" -ForegroundColor Green
Write-Host "  ZIP: $zipPath ($([math]::Round($zipSize, 2)) MB)"
Write-Host ""
Write-Host "SDK package ready for distribution!" -ForegroundColor Cyan
