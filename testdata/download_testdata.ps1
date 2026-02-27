#Requires -Version 5.1
<#
.SYNOPSIS
    Download and generate multi-scenario test audio for VoicePrint SDK.

.DESCRIPTION
    Downloads real speech/noise WAV files from public GitHub repos and
    generates synthetic test audio (pure-tone / multi-speaker) using Python
    or .NET when Python is not available.

    Sources:
      - speechbrain/speechbrain  (speech, VAD, separation, noise, language)
      - Azure-Samples/cognitive-services-speech-sdk  (additional speech)
      - Locally generated synthetic signals

    All output WAV files are placed under testdata/ subdirectories and are
    16 kHz, 16-bit, mono unless the source differs.

.OUTPUTS
    testdata/
      speech/         single-speaker utterances
      vad/            speech-with-silence segments for VAD testing
      multi_speaker/  mixtures of two speakers (diarization)
      noise/          background noise signals
      language/       one folder per language
        english/
        french/
        chinese/      (synthetic — no license-free Mandarin source found)
        german/       (synthetic)
      quality/        clean / low-SNR / clipping variants
      gender/         male-labelled and female-labelled samples
      antispoof/      genuine samples; spoofed samples (synthetic only)
      synthetic/      programmatically generated test signals
#>

param(
    [string]$TargetDir  = "$PSScriptRoot",
    [switch]$SkipSynth,          # skip Python synthesis step
    [switch]$Force               # re-download even if file already exists
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────
function Download-File {
    param([string]$Url, [string]$Dest)
    if ((Test-Path $Dest) -and -not $Force) {
        Write-Host "  [skip] $(Split-Path $Dest -Leaf)" -ForegroundColor DarkGray
        return $true
    }
    $dir = Split-Path $Dest -Parent
    if (-not (Test-Path $dir)) { New-Item $dir -ItemType Directory -Force | Out-Null }
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing -TimeoutSec 30
        $sz = (Get-Item $Dest).Length
        if ($sz -lt 100) { Remove-Item $Dest -Force; throw "suspiciously small file ($sz bytes)" }
        Write-Host "  [ok]   $(Split-Path $Dest -Leaf)  ($sz bytes)" -ForegroundColor Green
        return $true
    } catch {
        Write-Warning "  [fail] $Url  →  $_"
        return $false
    }
}

function New-Dir { param([string]$p)
    if (-not (Test-Path $p)) { New-Item $p -ItemType Directory -Force | Out-Null }
}

# ──────────────────────────────────────────────────────────────────────────────
# Directory layout
# ──────────────────────────────────────────────────────────────────────────────
$dirs = @(
    "speech", "vad", "multi_speaker", "noise",
    "language/english", "language/french", "language/chinese", "language/german",
    "quality/clean", "quality/noisy", "quality/clipped",
    "gender/male", "gender/female",
    "antispoof/genuine", "antispoof/spoofed",
    "synthetic"
)
foreach ($d in $dirs) { New-Dir (Join-Path $TargetDir $d) }

# ──────────────────────────────────────────────────────────────────────────────
# Base URL helpers
# ──────────────────────────────────────────────────────────────────────────────
$SB  = "https://raw.githubusercontent.com/speechbrain/speechbrain/develop/tests/samples"
$AZ  = "https://raw.githubusercontent.com/Azure-Samples/cognitive-services-speech-sdk/master/sampledata/audiofiles"

# ──────────────────────────────────────────────────────────────────────────────
# 1. Single-speaker speech  (speechbrain/speechbrain -- single-mic)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[1/8] Downloading single-speaker speech..." -ForegroundColor Cyan
$speechFiles = @(
    @{ url = "$SB/single-mic/example1.wav"; dest = "speech/speech_en_f_01.wav" }
    @{ url = "$SB/single-mic/example5.wav"; dest = "speech/speech_en_m_01.wav" }
    @{ url = "$SB/single-mic/example6.wav"; dest = "speech/speech_en_f_02.wav" }
)
foreach ($f in $speechFiles) {
    Download-File $f.url (Join-Path $TargetDir $f.dest)
}

# Extra English speech from speechbrain ASR test set
$azureFiles = @(
    @{ url = "$SB/ASR/spk1_snt1.wav"; dest = "speech/speech_en_m_02.wav" }
    @{ url = "$SB/ASR/spk2_snt1.wav"; dest = "speech/speech_en_f_03.wav" }
    @{ url = "$SB/ASR/spk1_snt2.wav"; dest = "speech/speech_en_m_03.wav" }
)
foreach ($f in $azureFiles) {
    Download-File $f.url (Join-Path $TargetDir $f.dest)
}

# ──────────────────────────────────────────────────────────────────────────────
# 2. VAD test audio  (speechbrain/speechbrain -- VAD)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[2/8] Downloading VAD samples..." -ForegroundColor Cyan
$vadFiles = @(
    @{ url = "$SB/VAD/train.wav"; dest = "vad/vad_train.wav" }
    @{ url = "$SB/VAD/valid.wav"; dest = "vad/vad_valid.wav" }
)
foreach ($f in $vadFiles) {
    Download-File $f.url (Join-Path $TargetDir $f.dest)
}

# ──────────────────────────────────────────────────────────────────────────────
# 3. Multi-speaker / diarization  (speechbrain/speechbrain -- separation)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[3/8] Downloading multi-speaker mixtures..." -ForegroundColor Cyan
for ($i = 0; $i -le 3; $i++) {
    Download-File "$SB/separation/mixture_$i.wav" (Join-Path $TargetDir "multi_speaker/mixture_$i.wav")
    Download-File "$SB/separation/source1_$i.wav" (Join-Path $TargetDir "multi_speaker/spk1_$i.wav")
    Download-File "$SB/separation/source2_$i.wav" (Join-Path $TargetDir "multi_speaker/spk2_$i.wav")
}

# ──────────────────────────────────────────────────────────────────────────────
# 4. Background noise  (speechbrain/speechbrain -- noise)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[4/8] Downloading noise samples..." -ForegroundColor Cyan
for ($i = 1; $i -le 5; $i++) {
    Download-File "$SB/noise/noise$i.wav" (Join-Path $TargetDir "noise/noise_$i.wav")
}

# ──────────────────────────────────────────────────────────────────────────────
# 5. Language samples
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[5/8] Downloading language samples..." -ForegroundColor Cyan

# English — use single-mic files directly (lang/* are symlinks; raw GitHub won't follow them)
Download-File "$SB/single-mic/example1.wav" (Join-Path $TargetDir "language/english/en_01.wav")
Download-File "$SB/single-mic/example5.wav" (Join-Path $TargetDir "language/english/en_02.wav")
Download-File "$SB/single-mic/example6.wav" (Join-Path $TargetDir "language/english/en_03.wav")

# French — speechbrain ASR speaker (accent/language test placeholder)
# Note: speechbrain test data is English-only; fr_synth.wav is generated below
Download-File "$SB/ASR/spk1_snt1.wav"  (Join-Path $TargetDir "language/french/fr_01.wav")
Download-File "$SB/ASR/spk2_snt1.wav"  (Join-Path $TargetDir "language/french/fr_02.wav")

# JFK clip from openai/whisper (well-known English reference, MIT licence)
$whisperBase = "https://raw.githubusercontent.com/openai/whisper/main/tests"
Download-File "$whisperBase/jfk.flac" (Join-Path $TargetDir "language/english/en_jfk.flac")

Write-Host "  Note: Chinese and German samples will be generated synthetically." -ForegroundColor Yellow

# ──────────────────────────────────────────────────────────────────────────────
# 6. Quality variants  (reuse speech + noise, generate clipped)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[6/8] Preparing quality test set..." -ForegroundColor Cyan
# Clean samples — already in speech/, just copy two for convenience
$cleanSrc  = Join-Path $TargetDir "speech/speech_en_m_01.wav"
$noisySrc  = Join-Path $TargetDir "noise/noise_1.wav"
if (Test-Path $cleanSrc) {
    Copy-Item $cleanSrc (Join-Path $TargetDir "quality/clean/clean_speech.wav") -Force
}
if (Test-Path $noisySrc) {
    Copy-Item $noisySrc (Join-Path $TargetDir "quality/noisy/noise_only.wav") -Force
}

# ──────────────────────────────────────────────────────────────────────────────
# 7. Gender-labelled  (reuse downloads, add a note file)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[7/8] Building gender-labelled set..." -ForegroundColor Cyan
# Male: example5.wav is a male speaker in speechbrain  
$maleSrc = Join-Path $TargetDir "speech/speech_en_m_01.wav"
if (Test-Path $maleSrc)   { Copy-Item $maleSrc   (Join-Path $TargetDir "gender/male/male_01.wav") -Force }
# Female: example1 and example6
$fem1 = Join-Path $TargetDir "speech/speech_en_f_01.wav"
$fem2 = Join-Path $TargetDir "speech/speech_en_f_02.wav"
if (Test-Path $fem1) { Copy-Item $fem1 (Join-Path $TargetDir "gender/female/female_01.wav") -Force }
if (Test-Path $fem2) { Copy-Item $fem2 (Join-Path $TargetDir "gender/female/female_02.wav") -Force }

# ──────────────────────────────────────────────────────────────────────────────
# 8. Synthetic audio generation  (Python)
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n[8/8] Generating synthetic test audio..." -ForegroundColor Cyan

if ($SkipSynth) {
    Write-Host "  Skipped (--SkipSynth)" -ForegroundColor Yellow
} else {
    $pythonExe = $null
    foreach ($candidate in @("python", "python3", "py")) {
        try {
            $ver = & $candidate --version 2>&1
            if ($ver -match "Python 3") { $pythonExe = $candidate; break }
        } catch {}
    }

    if ($null -ne $pythonExe) {
        Write-Host "  Using $pythonExe to generate synthetic WAV files..."
        $genScript = Join-Path $TargetDir "generate_synthetic.py"
        $outDir    = $TargetDir

        $pythonCode = @"
#!/usr/bin/env python3
"""Generate synthetic test audio covering various voice analysis scenarios."""
import struct, math, os, random, array, pathlib

SR   = 16000   # 16 kHz
BITS = 16
MAX  = 32767

def write_wav(path, samples, sr=SR):
    """Write float32 samples (range -1..1) as 16-bit mono WAV."""
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    data = array.array('h', [max(-MAX, min(MAX, int(s * MAX))) for s in samples])
    num_channels, sw = 1, 2
    byte_rate  = sr * num_channels * sw
    block_align = num_channels * sw
    data_bytes  = data.tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + len(data_bytes)))
        f.write(b'WAVE')
        f.write(b'fmt ')
        f.write(struct.pack('<IHHIIHH', 16, 1, num_channels, sr,
                            byte_rate, block_align, BITS))
        f.write(b'DATA')
        f.write(struct.pack('<I', len(data_bytes)))
        f.write(data_bytes)

def sine(freq, dur_s, amp=0.5, sr=SR):
    return [amp * math.sin(2*math.pi*freq*t/sr) for t in range(int(dur_s*sr))]

def voiced_buzz(f0, dur_s, amp=0.45, sr=SR):
    """Pulse-train (sawtooth) filtered through a very simple vocal-tract model."""
    n = int(dur_s * sr)
    period = sr / f0
    sig = []
    phase = 0.0
    for _ in range(n):
        sig.append(amp * (2 * (phase / period) - 1))  # sawtooth
        phase += 1
        if phase >= period:
            phase -= period
    # simple 2-pole resonance at ~800 Hz (formant F1)
    b0 = 0.02
    a1, a2 = -1.60, 0.70
    y = [0.0, 0.0]
    out = []
    for x in sig:
        new_y = b0 * x - a1 * y[-1] - a2 * y[-2]
        out.append(new_y)
        y.append(new_y)
        y.pop(0)
    return out

def add_noise(sig, snr_db):
    rms_s = math.sqrt(sum(x**2 for x in sig) / len(sig)) or 1e-9
    rms_n = rms_s / (10 ** (snr_db / 20))
    return [x + random.gauss(0, rms_n) for x in sig]

def silence(dur_s, sr=SR):
    return [0.0] * int(dur_s * sr)

def concat(*segs):
    out = []
    for s in segs: out.extend(s)
    return out

BASE = r"${outDir}".replace('\\', '/')

# ── 1. Synthetic male voice (F0 ≈ 120 Hz, 4 s) ─────────────────────────────
write_wav(f"{BASE}/gender/male/male_synth.wav",
          voiced_buzz(120, 4.0))

# ── 2. Synthetic female voice (F0 ≈ 230 Hz, 4 s) ────────────────────────────
write_wav(f"{BASE}/gender/female/female_synth.wav",
          voiced_buzz(230, 4.0))

# ── 3. Child-like voice (F0 ≈ 350 Hz, 3 s) ───────────────────────────────────
write_wav(f"{BASE}/gender/child_synth.wav",
          voiced_buzz(350, 3.0, amp=0.35))

# ── 4. High-energy / stressed voice (buzz + pitch wobble) ────────────────────
def stressed_voice(dur_s=4.0):
    n = int(dur_s * SR)
    sig = []
    for t in range(n):
        f0 = 160 + 30 * math.sin(2 * math.pi * 0.8 * t / SR)  # pitch oscillation
        sig.append(0.6 * math.sin(2 * math.pi * f0 * t / SR))
    return sig
write_wav(f"{BASE}/synthetic/stressed_voice.wav", stressed_voice())

# ── 5. Fatigued / breathy voice (reduced harmonics, more aspiration noise) ───
def breathy_voice(dur_s=4.0):
    buzz  = voiced_buzz(140, dur_s, amp=0.25)
    noise = [0.12 * random.gauss(0, 1) for _ in buzz]
    return [b + n for b, n in zip(buzz, noise)]
write_wav(f"{BASE}/synthetic/breathy_voice.wav", breathy_voice())

# ── 6. Clean speech (buzz, high SNR) ─────────────────────────────────────────
write_wav(f"{BASE}/quality/clean/synth_clean.wav",
          voiced_buzz(180, 5.0, amp=0.5))

# ── 7. Noisy speech (10 dB SNR) ───────────────────────────────────────────────
clean = voiced_buzz(180, 5.0, amp=0.5)
write_wav(f"{BASE}/quality/noisy/synth_snr10dB.wav",
          add_noise(clean, 10))

# ── 8. Low SNR speech (5 dB) ──────────────────────────────────────────────────
write_wav(f"{BASE}/quality/noisy/synth_snr5dB.wav",
          add_noise(clean, 5))

# ── 9. Clipped speech ────────────────────────────────────────────────────────
write_wav(f"{BASE}/quality/clipped/synth_clipped.wav",
          [min(0.25, max(-0.25, x * 3)) for x in clean])  # hard clip at 25%

# ── 10. Multi-speaker synthetic (alternating spk1 / spk2 / silence) ──────────
spk1 = voiced_buzz(130, 2.0)
spk2 = voiced_buzz(210, 2.0)
multi = concat(spk1, silence(0.5), spk2, silence(0.5),
               spk1, silence(0.3), spk2, silence(0.5), spk1, spk2)
write_wav(f"{BASE}/multi_speaker/synth_2spk.wav", multi)

# ── 11. Diarization: 3-speaker round-robin ────────────────────────────────────
spk3 = voiced_buzz(280, 1.5)
tri  = concat(spk1[:SR*2], silence(0.4), spk2[:SR*2], silence(0.4),
              spk3[:SR*2], silence(0.4), spk1[:SR*1],
              silence(0.3), spk2[:SR*1])
write_wav(f"{BASE}/multi_speaker/synth_3spk.wav", tri)

# ── 12. VAD test: speech + long silence + speech ─────────────────────────────
vad_test = concat(voiced_buzz(160, 2.0), silence(3.0), voiced_buzz(200, 2.0),
                  silence(1.5), voiced_buzz(180, 1.5))
write_wav(f"{BASE}/vad/synth_vad_test.wav", vad_test)

# ── 13. Language / accent variants (different F0 / tempo signatures) ─────────
# Mandarin-like: tonal, narrow pitch range
def tonal_voice(dur_s=4.0):
    n = int(dur_s * SR)
    tones = [210, 240, 170, 210]   # simplified 4-tone F0 pattern
    seg_n = n // len(tones)
    sig = []
    for f in tones:
        sig.extend(voiced_buzz(f, seg_n / SR))
    return sig[:n]
write_wav(f"{BASE}/language/chinese/zh_synth.wav", tonal_voice())

# German-like: lower pitch, slightly longer sentences
write_wav(f"{BASE}/language/german/de_synth.wav", voiced_buzz(135, 5.0))

# ── 14. Anti-spoof: "spoofed" = pure-sine, no natural harmonics ──────────────
spoof = sine(200, 5.0, amp=0.5)   # artificial / TTS-like
write_wav(f"{BASE}/antispoof/spoofed/synth_pure_sine.wav", spoof)
# Genuine (natural buzz)
genuine = voiced_buzz(170, 5.0)
write_wav(f"{BASE}/antispoof/genuine/synth_genuine.wav", genuine)

print("Synthetic audio generated successfully.")
"@

        Set-Content -Path $genScript -Value $pythonCode -Encoding UTF8
        try {
            & $pythonExe $genScript
            Write-Host "  Synthetic audio written." -ForegroundColor Green
        } catch {
            Write-Warning "  Python synthesis failed: $_"
        }
        Remove-Item $genScript -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host "  Python 3 not found — generating minimal WAV files via .NET..." -ForegroundColor Yellow
        # Fallback: write a minimal 16-bit sine-wave WAV using .NET BinaryWriter
        function Write-SineWav {
            param([string]$Path, [float]$FreqHz, [float]$DurSec, [int]$Sr=16000)
            $n = [int]($Sr * $DurSec)
            $dir = Split-Path $Path -Parent
            if (-not (Test-Path $dir)) { New-Item $dir -ItemType Directory -Force | Out-Null }
            $fs  = [System.IO.File]::Create($Path)
            $bw  = New-Object System.IO.BinaryWriter($fs)
            $dataLen = $n * 2
            # RIFF header
            $bw.Write([byte[]][System.Text.Encoding]::ASCII.GetBytes("RIFF"))
            $bw.Write([int32](36 + $dataLen))
            $bw.Write([byte[]][System.Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
            $bw.Write([int32]16)
            $bw.Write([int16]1)  # PCM
            $bw.Write([int16]1)  # mono
            $bw.Write([int32]$Sr)
            $bw.Write([int32]($Sr * 2))
            $bw.Write([int16]2)
            $bw.Write([int16]16)
            $bw.Write([byte[]][System.Text.Encoding]::ASCII.GetBytes("data"))
            $bw.Write([int32]$dataLen)
            for ($t = 0; $t -lt $n; $t++) {
                $s = [int16](0.45 * 32767 * [math]::Sin(2 * [math]::PI * $FreqHz * $t / $Sr))
                $bw.Write($s)
            }
            $bw.Close(); $fs.Close()
        }
        # Gender
        Write-SineWav (Join-Path $TargetDir "gender/male/male_synth.wav")          120  4
        Write-SineWav (Join-Path $TargetDir "gender/female/female_synth.wav")       230  4
        Write-SineWav (Join-Path $TargetDir "gender/child_synth.wav")               350  3
        # Quality
        Write-SineWav (Join-Path $TargetDir "quality/clean/synth_clean.wav")        180  5
        Write-SineWav (Join-Path $TargetDir "quality/noisy/synth_snr10dB.wav")      185  5
        Write-SineWav (Join-Path $TargetDir "quality/noisy/synth_snr5dB.wav")       175  5
        Write-SineWav (Join-Path $TargetDir "quality/clipped/synth_clipped.wav")    180  5
        # VAD / multi-speaker
        Write-SineWav (Join-Path $TargetDir "vad/synth_vad_test.wav")               160  8
        Write-SineWav (Join-Path $TargetDir "multi_speaker/synth_2spk.wav")         145  8
        Write-SineWav (Join-Path $TargetDir "multi_speaker/synth_3spk.wav")         200  9
        # Anti-spoof
        Write-SineWav (Join-Path $TargetDir "antispoof/spoofed/synth_pure_sine.wav") 200 5
        Write-SineWav (Join-Path $TargetDir "antispoof/genuine/synth_genuine.wav")   170 5
        # Synthetic state
        Write-SineWav (Join-Path $TargetDir "synthetic/test_tone_200Hz.wav")         200 3
        Write-SineWav (Join-Path $TargetDir "synthetic/stressed_voice.wav")          160 4
        Write-SineWav (Join-Path $TargetDir "synthetic/breathy_voice.wav")           140 4
        # Language synthetic
        Write-SineWav (Join-Path $TargetDir "language/chinese/zh_synth.wav")         210 4
        Write-SineWav (Join-Path $TargetDir "language/german/de_synth.wav")          135 5
        Write-SineWav (Join-Path $TargetDir "language/french/fr_synth.wav")          190 4
        Write-Host "  Minimal synthetic WAVs written via .NET." -ForegroundColor Green
    }
}

# ──────────────────────────────────────────────────────────────────────────────
# Summary
# ──────────────────────────────────────────────────────────────────────────────
Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host " Download complete.  Directory summary:" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Get-ChildItem $TargetDir -Recurse -Filter "*.wav" |
    Group-Object { $_.DirectoryName.Replace($TargetDir,'').TrimStart('\').TrimStart('/') } |
    Sort-Object Name |
    ForEach-Object {
        $cnt  = $_.Count
        $size = ($_.Group | Measure-Object Length -Sum).Sum
        Write-Host ("  {0,-35}  {1,3} file(s)  {2,7:N0} KB" -f $_.Name, $cnt, ($size/1KB))
    }
Write-Host ""

# Also list any .flac files downloaded
$flacs = @(Get-ChildItem $TargetDir -Recurse -Filter "*.flac" -ErrorAction SilentlyContinue)
if ($flacs.Count -gt 0) {
    Write-Host "  FLAC files (may need ffmpeg to convert to WAV):" -ForegroundColor Yellow
    $flacs | ForEach-Object { Write-Host "    $_" }
}

Write-Host "`nTip: Re-run with -Force to re-download all files." -ForegroundColor DarkGray
Write-Host "Tip: Run with -SkipSynth to skip the synthetic generation." -ForegroundColor DarkGray
