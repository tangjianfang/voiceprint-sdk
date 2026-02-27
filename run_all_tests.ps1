<#
.SYNOPSIS
    VoicePrint SDK — 一键全量测试脚本

.DESCRIPTION
    功能：
      1. 构建 SDK（可选，默认跳过如果 build/ 已存在）
      2. 运行测试音频下载（可选）
      3. 运行单元测试      (unit_tests.exe)
      4. 运行集成测试      (integration_tests.exe)
      5. 运行 API 全量测试 (api_tests.exe)       ← 覆盖所有接口 + 测试音频
      6. 运行性能基准测试  (benchmark_tests.exe)
      7. 合并所有报告到    reports/full_report.md

.PARAMETER Build
    强制重新构建（cmake configure + build）

.PARAMETER DownloadTestdata
    运行 testdata/download_testdata.ps1 下载测试音频

.PARAMETER SkipUnit
    跳过单元测试

.PARAMETER SkipIntegration
    跳过集成测试

.PARAMETER SkipBenchmark
    跳过性能基准测试

.PARAMETER SkipApiTest
    跳过 API 全量测试（主测试）

.PARAMETER Config
    构建配置（Debug 或 Release），默认 Release

.EXAMPLE
    # 最简一键运行（假设已经构建过）：
    .\run_all_tests.ps1

    # 先下载音频，再全量测试：
    .\run_all_tests.ps1 -DownloadTestdata

    # 强制重新构建并测试：
    .\run_all_tests.ps1 -Build

    # 只跑 API 测试，其余跳过：
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

$ROOT     = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BUILD    = Join-Path $ROOT "build"
$BIN      = Join-Path $BUILD "bin\$Config"
$MODELS   = Join-Path $ROOT "models"
$TESTDATA = Join-Path $ROOT "testdata"
$REPORTS  = Join-Path $ROOT "reports"

# ANSI color helpers
function Green($s)  { Write-Host $s -ForegroundColor Green  }
function Yellow($s) { Write-Host $s -ForegroundColor Yellow }
function Red($s)    { Write-Host $s -ForegroundColor Red    }
function Cyan($s)   { Write-Host $s -ForegroundColor Cyan   }
function White($s)  { Write-Host $s -ForegroundColor White  }

# ──────────────────────────────────────────────────
# Summary tracking
# ──────────────────────────────────────────────────
$Results = [System.Collections.Generic.List[PSObject]]::new()

function Add-Result($Name, $Passed, $Detail = "") {
    $Results.Add([PSCustomObject]@{ Name=$Name; Passed=$Passed; Detail=$Detail })
}

# ──────────────────────────────────────────────────
# Header
# ──────────────────────────────────────────────────
$StartTime = Get-Date
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
Cyan "   VoicePrint SDK — 一键全量测试"
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
White "  时间:    $($StartTime.ToString('yyyy-MM-dd HH:mm:ss'))"
White "  根目录:  $ROOT"
White "  构建目录: $BUILD"
White "  模型目录: $MODELS"
White ""

# ──────────────────────────────────────────────────
# Step 0: Download testdata (optional)
# ──────────────────────────────────────────────────
if ($DownloadTestdata) {
    Cyan "`n[Step 0] 下载测试音频..."
    $dlScript = Join-Path $TESTDATA "download_testdata.ps1"
    if (Test-Path $dlScript) {
        Push-Location $TESTDATA
        & $dlScript
        if ($LASTEXITCODE -eq 0) { Green "  下载完成" ; Add-Result "下载测试音频" $true }
        else                     { Yellow "  下载部分失败（继续）"; Add-Result "下载测试音频" $false "exit=$LASTEXITCODE" }
        Pop-Location
    } else {
        Yellow "  testdata/download_testdata.ps1 不存在，跳过"
    }
}

# ──────────────────────────────────────────────────
# Step 1: Build
# ──────────────────────────────────────────────────
if ($Build -or !(Test-Path (Join-Path $BUILD "voiceprint-sdk.sln"))) {
    Cyan "`n[Step 1] CMake 构建..."

    if (!(Test-Path $BUILD)) { New-Item -ItemType Directory -Path $BUILD | Out-Null }

    Write-Host "  cmake configure..."
    $cmakeArgs = @(
        "-B", $BUILD,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$Config"
    )
    & cmake $cmakeArgs 2>&1 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) {
        Red "  CMake configure 失败 (exit $LASTEXITCODE)"
        Add-Result "CMake Configure" $false "exit=$LASTEXITCODE"
    } else {
        Write-Host "  cmake build..."
        & cmake --build $BUILD --config $Config --parallel 2>&1 | ForEach-Object { Write-Host "    $_" }
        if ($LASTEXITCODE -ne 0) {
            Red "  Build 失败 (exit $LASTEXITCODE)"
            Add-Result "CMake Build" $false "exit=$LASTEXITCODE"
        } else {
            Green "  Build 成功"
            Add-Result "CMake Build" $true
        }
    }
} else {
    Yellow "`n[Step 1] 跳过构建（$BUILD 已存在，使用 -Build 强制重建）"
}

# Check binaries exist
function Require-Exe($name) {
    $path = Join-Path $BIN "$name.exe"
    if (!(Test-Path $path)) {
        Yellow "  警告: $name.exe 不存在于 $BIN"
        return $null
    }
    return $path
}

# Create reports dir
if (!(Test-Path $REPORTS)) { New-Item -ItemType Directory -Path $REPORTS | Out-Null }

# ──────────────────────────────────────────────────
# Step 2: Unit Tests
# ──────────────────────────────────────────────────
if (!$SkipUnit) {
    Cyan "`n[Step 2] 单元测试 (unit_tests)..."
    $exe = Require-Exe "unit_tests"
    if ($exe) {
        Push-Location $ROOT
        $out = & $exe --gtest_output="xml:reports/unit_tests.xml" 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL" }
        Add-Result "单元测试" $passed "exit=$LASTEXITCODE"
        Pop-Location
    } else {
        Add-Result "单元测试" $false "exe 不存在"
    }
} else {
    Yellow "`n[Step 2] 跳过单元测试 (-SkipUnit)"
}

# ──────────────────────────────────────────────────
# Step 3: Integration Tests
# ──────────────────────────────────────────────────
if (!$SkipIntegration) {
    Cyan "`n[Step 3] 集成测试 (integration_tests)..."
    $exe = Require-Exe "integration_tests"
    if ($exe) {
        Push-Location $ROOT
        $out = & $exe --gtest_output="xml:reports/integration_tests.xml" 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL" }
        Add-Result "集成测试" $passed "exit=$LASTEXITCODE"
        Pop-Location
    } else {
        Add-Result "集成测试" $false "exe 不存在"
    }
} else {
    Yellow "`n[Step 3] 跳过集成测试 (-SkipIntegration)"
}

# ──────────────────────────────────────────────────
# Step 4: API Comprehensive Test  ← 核心
# ──────────────────────────────────────────────────
if (!$SkipApiTest) {
    Cyan "`n[Step 4] API 全量测试 (api_tests) — 覆盖全部接口..."
    $exe = Require-Exe "api_tests"
    if ($exe) {
        Push-Location $ROOT
        $apiReport = Join-Path $REPORTS "api_test_report.md"
        $out = & $exe `
            --models   $MODELS `
            --testdata $TESTDATA `
            --report   $apiReport `
            2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL (详见 $apiReport)" }
        Add-Result "API 全量测试" $passed "exit=$LASTEXITCODE | 报告: $apiReport"
        Pop-Location
    } else {
        Add-Result "API 全量测试" $false "api_tests.exe 不存在，请先构建 (-Build)"
    }
} else {
    Yellow "`n[Step 4] 跳过 API 全量测试 (-SkipApiTest)"
}

# ──────────────────────────────────────────────────
# Step 5: Benchmark
# ──────────────────────────────────────────────────
if (!$SkipBenchmark) {
    Cyan "`n[Step 5] 性能基准测试 (benchmark_tests)..."
    $exe = Require-Exe "benchmark_tests"
    if ($exe) {
        Push-Location $ROOT
        $out = & $exe $MODELS 2>&1
        $passed = $LASTEXITCODE -eq 0
        $out | ForEach-Object { Write-Host "  $_" }
        if ($passed) { Green "  PASS" } else { Red "  FAIL" }
        Add-Result "性能基准测试" $passed "exit=$LASTEXITCODE | 报告: reports/benchmark_report.txt"
        Pop-Location
    } else {
        Add-Result "性能基准测试" $false "exe 不存在"
    }
} else {
    Yellow "`n[Step 5] 跳过性能基准测试 (-SkipBenchmark)"
}

# ──────────────────────────────────────────────────
# Step 6: Write merged report
# ──────────────────────────────────────────────────
$EndTime  = Get-Date
$TotalSec = [int]($EndTime - $StartTime).TotalSeconds

$passCount = ($Results | Where-Object { $_.Passed }).Count
$failCount = ($Results | Where-Object { !$_.Passed }).Count
$allPass   = $failCount -eq 0

Cyan "`n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
Cyan "  测试汇总"
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

foreach ($r in $Results) {
    $icon = if ($r.Passed) { "✅" } else { "❌" }
    $line = "  $icon $($r.Name)"
    if ($r.Detail) { $line += "  ($($r.Detail))" }
    if ($r.Passed) { Green $line } else { Red $line }
}

White ""
White "  通过: $passCount  失败: $failCount  总用时: ${TotalSec}s"
if ($allPass) { Green "  全部通过 🎉" } else { Red "  存在失败项，请查看报告" }

# Write merged Markdown report
$fullReport = Join-Path $REPORTS "full_report.md"
$ts = $StartTime.ToString("yyyy-MM-dd HH:mm:ss")
$reportLines = @(
    "# VoicePrint SDK — 全量测试报告",
    "",
    "| 项目 | 值 |",
    "|------|-----|",
    "| 测试时间 | $ts |",
    "| 总用时 | ${TotalSec}s |",
    "| 通过 | $passCount / $($Results.Count) |",
    "| 失败 | $failCount |",
    "",
    "## 阶段汇总",
    "",
    "| 阶段 | 结果 | 说明 |",
    "|------|------|------|"
)
foreach ($r in $Results) {
    $status = if ($r.Passed) { "✅ PASS" } else { "❌ FAIL" }
    $reportLines += "| $($r.Name) | $status | $($r.Detail) |"
}

$reportLines += @(
    "",
    "## 详细报告链接",
    "",
    "- [API 全量测试报告](api_test_report.md)",
    "- [性能基准报告](benchmark_report.txt)",
    "",
    "---",
    "*由 run_all_tests.ps1 自动生成*"
)

$reportLines | Set-Content $fullReport -Encoding UTF8
White ""
White "  完整报告: $fullReport"
Cyan "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

exit ($allPass ? 0 : 1)
