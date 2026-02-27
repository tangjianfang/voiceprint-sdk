# VoicePrint SDK — Test Audio Data

This directory holds test audio for unit tests, integration tests, evaluation,
and manual validation of every SDK feature.

---

## Quick Start

```powershell
# Download real-speech samples from GitHub + generate synthetic audio
cd testdata
.\download_testdata.ps1

# Re-download everything (overwrite existing files)
.\download_testdata.ps1 -Force

# Skip the Python-based synthesis step
.\download_testdata.ps1 -SkipSynth
```

Requires PowerShell 5.1+ and internet access.
Python 3 (stdlib only) is optional but recommended for richer synthetic audio.
If Python is absent, minimal sine-wave WAVs are written via .NET instead.

---

## Directory Structure

```
testdata/
  speech/              Single-speaker utterances (English)
  vad/                 Voice-activity detection testing
  multi_speaker/       Diarization / speaker separation
  noise/               Background noise signals
  language/
    english/           en_01.wav, en_02.wav, en_jfk.flac
    french/            fr_01.wav, fr_02.wav
    chinese/           zh_synth.wav  (synthetic tonal F0 pattern)
    german/            de_synth.wav  (synthetic)
  quality/
    clean/             High-SNR reference speech
    noisy/             10 dB and 5 dB SNR variants
    clipped/           Hard-clipped speech
  gender/
    male/              Real + synthetic (F0 ~120 Hz)
    female/            Real + synthetic (F0 ~230 Hz)
    child_synth.wav    Synthetic child-like voice (F0 ~350 Hz)
  antispoof/
    genuine/           Natural voiced buzz
    spoofed/           Pure-tone (TTS-like artefact)
  synthetic/           Miscellaneous generated signals
```

---

## Audio Sources

| Source | License | URL |
|--------|---------|-----|
| speechbrain/speechbrain | Apache 2.0 | https://github.com/speechbrain/speechbrain |
| Azure-Samples/cognitive-services-speech-sdk | MIT | https://github.com/Azure-Samples/cognitive-services-speech-sdk |
| openai/whisper | MIT | https://github.com/openai/whisper |
| Synthetic (generated locally) | N/A | download_testdata.ps1 |

---

## Large-Scale Evaluation Datasets

For full EER/DCF evaluation, obtain one of these datasets separately:

### VoxCeleb1 Test Set
1. Register at https://www.robots.ox.ac.uk/~vgg/data/voxceleb/
2. Download vox1_test_wav.zip, extract to testdata/voxceleb1/
3. Place trial list at testdata/voxceleb1/trials.txt

### AISHELL-ASV (Mandarin)
- https://openslr.org/33/

### Custom Data
- 16 kHz WAV, mono, 16-bit PCM
- Minimum 3 s per utterance
- Format: testdata/trials.txt  (label enroll_wav test_wav)

---

## Format Requirements

All SDK functions expect 16 kHz, mono, 16-bit PCM WAV.
Use ffmpeg to convert:

  ffmpeg -i input.flac -ar 16000 -ac 1 -sample_fmt s16 output.wav
