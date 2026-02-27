<#
.SYNOPSIS
    VoicePrint SDK - One-click full test script

.PARAMETER Build
    Force rebuild (cmake configure + build)

.PARAMETER DownloadTestdata
    Run testdata/download_testdata.ps1 to download test audio

.PARAMETER SkipUnit
    Skip unit tests

.PARAMETER SkipIntegration
    Skip integration tests

.PARAMETER SkipBenchmark
    Skip benchmark tests

.PARAMETER SkipApiTest
    Skip API full test (the main test)

.PARAMETER Config
    Build configuration (Debug or Release), default Release

.EXAMPLE
    .\run_all_tests.ps1
    .\run_all_tests.ps1 -DownloadTestdata
    .\run_all_tests.ps1 -Build
    .\run_all_tests.ps1 -SkipUnit -SkipIntegration -SkipBenchmark
#>

param(
    [switch]$Build,
    [switch]$DownloadTestdata,
    [switch]$SkipUnit,
    [switch]$SkipIntegration,
    [switch]$SkipBenchmark,
    [switch]$SkipApiTest,
    [string]$Config = "Release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

$ROOT      = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BuildDir  = Join-Path $ROOT "build"
$BinDir    = Join-Path $BuildDir "bin\$Config"
$MODELS    = Join-Path $ROOT "models"
$TESTDATA  = Join-Path $ROOT "testdata"
$REPORTS   = Join-Path $ROOT "reports"

function Green($s)  { Write-Host $s -ForegroundColor Green  }
function Yellow($s) { Write-Host $s -ForegroundColor Yellow }
function Red($s)    { Write-Host $s -ForegroundColor Red    }
function Cyan($s)   { Write-Host $s -ForegroundColor Cyan   }
function White($s)  { Write-Host $s -ForegroundColor White  }

$Results = [System.Collections.Generic.List[PSObject]]::new()
function Add-Result($Name, $Passed, $Detail = "") {
    $Results.Add([PSCustomObject]@{ Name=$Name; Passed=$Passed; Detail=$Detail })
}

$StartTime = Get-Date
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
Cyan "   VoicePrint SDK - Full Test Suite"
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
White "  Time:      $($StartTime.ToString('yyyy-MM-dd HH:mm:ss'))"
White "  Root:      $ROOT"
White "  Build dir: $BuildDir"
White "  Models:    $MODELS"
White ""

if ($DownloadTestdata) {
    Cyan "`n[Step 0] Downloading test audio..."
    $dlScript = Join-Path $TESTDATA "download_testdata.ps1"
    if (Test-Path $dlScript) {
        Push-Location $TESTDATA
        & $dlScript
        if ($LASTEXITCODE -eq 0) { Green "  Download complete"; Add-Result "Download Testdata" $true }
        else { Yellow "  Download partially failed (continuing)"; Add-Result "Download Testdata" $false "exit=$LASTEXITCODE" }
        Pop-Location
    } else {
        Yellow "  testdata/download_testdata.ps1 not found, skipping"
    }
}

if ($Build -or !(Test-Path (Join-Path $BuildDir "voiceprint-sdk.sln"))) {
    Cyan "`n[Step 1] CMake Build..."
    if (!(Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }
    Write-Host "  cmake configure..."
    $cmakeArgs = @("-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DCMAKE_BUILD_TYPE=$Config")
    & cmake $cmakeArgs 2>&1 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) {
        Red "  CMake configure failed (exit $LASTEXITCODE)"
        Add-Result "CMake Configure" $false "exit=$LASTEXITCODE"
    } else {
        Write-Host "  cmake build..."
        & cmake --build $BuildDir --config $Config --parallel 2>&1 | ForEach-Object { Write-Host "    $_" }
        if ($LASTEXITCODE -ne 0) {
            Red "  Build failed (exit $LASTEXITCODE)"
            Add-Result "CMake Build" $false "exit=$LASTEXITCODE"
        } else {
            Green "  Build succeeded"
            Add-Result "CMake Build" $true
        }
    }
} else {
    Yellow "`n[Step 1] Skipping build ($BuildDir exists, use -Build to force rebuild)"
}

function Require-Exe($name) {
    $p = Join-Path $BinDir "$name.exe"
    if (!(Test-Path $p)) { Yellow "  Warning: $name.exe not found in $BinDir"; return $null }
    return $p
}

if (!(Test-Path $REPORTS)) { New-Item -ItemType Directory -Path $REPORTS | Out-Null }

if (!$SkipUnit) {
    Cyan "`n[Step 2] Unit Tests (unit_tests)..."
    $exe = Require-Exe "unit_tests"
    if ($exe) {
        Push-Location $ROOT
        $out = & $exe --gtest_output="xml:reports/unit_tests.xml" 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL" }
        Add-Result "Unit Tests" $passed "exit=$LASTEXITCODE"
        Pop-Location
    } else { Add-Result "Unit Tests" $false "exe not found" }
} else { Yellow "`n[Step 2] Skipping unit tests (-SkipUnit)" }

if (!$SkipIntegration) {
    Cyan "`n[Step 3] Integration Tests (integration_tests)..."
    $exe = Require-Exe "integration_tests"
    if ($exe) {
        Push-Location $ROOT
        $out = & $exe --gtest_output="xml:reports/integration_tests.xml" 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL" }
        Add-Result "Integration Tests" $passed "exit=$LASTEXITCODE"
        Pop-Location
    } else { Add-Result "Integration Tests" $false "exe not found" }
} else { Yellow "`n[Step 3] Skipping integration tests (-SkipIntegration)" }

if (!$SkipApiTest) {
    Cyan "`n[Step 4] API Full Test (api_tests) - covers all public APIs..."
    $exe = Require-Exe "api_tests"
    if ($exe) {
        Push-Location $ROOT
        $apiReport = Join-Path $REPORTS "api_test_report.md"
        $out = & $exe --models $MODELS --testdata $TESTDATA --report $apiReport 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL (see $apiReport)" }
        Add-Result "API Full Test" $passed "exit=$LASTEXITCODE | report: $apiReport"
        Pop-Location
    } else { Add-Result "API Full Test" $false "api_tests.exe not found, build first (-Build)" }
} else { Yellow "`n[Step 4] Skipping API full test (-SkipApiTest)" }

if (!$SkipBenchmark) {
    Cyan "`n[Step 5] Benchmark Tests (benchmark_tests)..."
    $exe = Require-Exe "benchmark_tests"
    if ($exe) {
        Push-Location $ROOT
        $out = & $exe $MODELS 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL" }
        Add-Result "Benchmark Tests" $passed "exit=$LASTEXITCODE | report: reports/benchmark_report.txt"
        Pop-Location
    } else { Add-Result "Benchmark Tests" $false "exe not found" }
} else { Yellow "`n[Step 5] Skipping benchmark tests (-SkipBenchmark)" }

$EndTime  = Get-Date
$TotalSec = [int]($EndTime - $StartTime).TotalSeconds
$passCount = @($Results | Where-Object { $_.Passed -eq $true }).Count
$failCount = @($Results | Where-Object { $_.Passed -eq $false }).Count
$allPass   = $failCount -eq 0

Cyan "`n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
Cyan "  Test Summary"
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
foreach ($r in $Results) {
    $icon = if ($r.Passed) { "[PASS]" } else { "[FAIL]" }
    $line = "  $icon $($r.Name)"
    if ($r.Detail) { $line += "  ($($r.Detail))" }
    if ($r.Passed) { Green $line } else { Red $line }
}
White ""
White "  Passed: $passCount  Failed: $failCount  Total time: ${TotalSec}s"
if ($allPass) { Green "  All tests passed!" } else { Red "  Some tests failed, check reports." }

$fullReport = Join-Path $REPORTS "full_report.md"
$ts = $StartTime.ToString("yyyy-MM-dd HH:mm:ss")
$rp = [System.Collections.Generic.List[string]]::new()
$rp.Add("# VoicePrint SDK - Full Test Report")
$rp.Add("")
$rp.Add("| Item | Value |")
$rp.Add("|------|-------|")
$rp.Add("| Test Time | $ts |")
$rp.Add("| Duration  | ${TotalSec}s |")
$rp.Add("| Passed    | $passCount / $($Results.Count) |")
$rp.Add("| Failed    | $failCount |")
$rp.Add("")
$rp.Add("## Stage Summary")
$rp.Add("")
$rp.Add("| Stage | Result | Detail |")
$rp.Add("|-------|--------|--------|")
foreach ($r in $Results) {
    $status = if ($r.Passed) { "PASS" } else { "FAIL" }
    $rp.Add("| $($r.Name) | $status | $($r.Detail) |")
}
$rp.Add("")
$rp.Add("## Detailed Report Links")
$rp.Add("")
$rp.Add("- [API Full Test Report](api_test_report.md)")
$rp.Add("- [Benchmark Report](benchmark_report.txt)")
$rp.Add("")
$rp.Add("---")
$rp.Add("*Auto-generated by run_all_tests.ps1*")
[System.IO.File]::WriteAllLines($fullReport, $rp, [System.Text.Encoding]::UTF8)

White "  Full report: $fullReport"
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if ($allPass) { exit 0 } else { exit 1 }
