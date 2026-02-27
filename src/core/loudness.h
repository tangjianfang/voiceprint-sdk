#pragma once
#ifndef VP_LOUDNESS_H
#define VP_LOUDNESS_H

// ITU-R BS.1770-4 integrated loudness measurement (K-weighting filter)
// All processing is done at 16kHz mono (SDK standard).

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace vp {
namespace dsp {

// ----------------------------------------------------------------
// K-weighting filter coefficients for 16kHz sample rate.
// Stage 1: high-shelf pre-filter  (head acoustics)
// Stage 2: high-pass filter       (removes DC / sub-bass)
// Both are biquad IIR in Direct Form I.
// Coefficients computed offline with scipy.signal at fs=16000.
// ----------------------------------------------------------------
struct BiquadState { float x1=0, x2=0, y1=0, y2=0; };

static float biquad_tick(float x, BiquadState& s,
                         float b0, float b1, float b2,
                         float a1, float a2) {
    float y = b0*x + b1*s.x1 + b2*s.x2 - a1*s.y1 - a2*s.y2;
    s.x2 = s.x1; s.x1 = x;
    s.y2 = s.y1; s.y1 = y;
    return y;
}

// Stage-1 high-shelf (Butterworth-like) at 16kHz
// b = [1.5303, -2.6906,  1.1983]  a = [1.0, -1.6636,  0.7134]
static constexpr float HS_B0 =  1.5303f, HS_B1 = -2.6906f, HS_B2 =  1.1983f;
static constexpr float HS_A1 = -1.6636f, HS_A2 =  0.7134f;

// Stage-2 high-pass (100 Hz) at 16kHz
// b = [0.9961, -1.9922,  0.9961]  a = [1.0, -1.9921,  0.9924]
static constexpr float HP_B0 =  0.9961f, HP_B1 = -1.9922f, HP_B2 =  0.9961f;
static constexpr float HP_A1 = -1.9921f, HP_A2 =  0.9924f;

// ----------------------------------------------------------------
// Compute integrated loudness (LUFS) per BS.1770-4
// Block size: 400ms (6400 samples at 16kHz), overlap: 75% (step 1600)
// ----------------------------------------------------------------
inline float compute_lufs(const std::vector<float>& pcm, int sample_rate = 16000) {
    if (pcm.empty()) return -70.0f;

    // Apply K-weighting filter chain
    std::vector<float> filtered(pcm.size());
    BiquadState hs, hp;
    for (size_t i = 0; i < pcm.size(); ++i) {
        float y1 = biquad_tick(pcm[i], hs, HS_B0, HS_B1, HS_B2, HS_A1, HS_A2);
        filtered[i] = biquad_tick(y1,    hp, HP_B0, HP_B1, HP_B2, HP_A1, HP_A2);
    }

    // Gated measurement: 400ms blocks, 75% overlap
    const int block_size = static_cast<int>(0.4 * sample_rate);   // 6400 @ 16k
    const int hop_size   = static_cast<int>(0.1 * sample_rate);   // 1600 @ 16k
    const int n = static_cast<int>(pcm.size());

    // Pre-compute mean-square for each block
    std::vector<float> block_ms;
    block_ms.reserve((n - block_size) / hop_size + 2);
    for (int start = 0; start + block_size <= n; start += hop_size) {
        double sum = 0.0;
        for (int j = start; j < start + block_size; ++j)
            sum += static_cast<double>(filtered[j]) * filtered[j];
        block_ms.push_back(static_cast<float>(sum / block_size));
    }

    if (block_ms.empty()) {
        // Short audio fallback: single block mean-square
        double sum = 0.0;
        for (float s : filtered) sum += static_cast<double>(s)*s;
        float ms = static_cast<float>(sum / filtered.size());
        return ms > 1e-10f ? 10.0f * std::log10(ms) - 0.691f : -70.0f;
    }

    // Absolute gate: discard blocks below -70 LUFS
    const float abs_threshold_ms = static_cast<float>(std::pow(10.0, (-70.0 - 0.691) / 10.0));
    std::vector<float> above_abs;
    for (float ms : block_ms)
        if (ms >= abs_threshold_ms) above_abs.push_back(ms);

    if (above_abs.empty()) return -70.0f;

    // Relative gate: mean of above-abs blocks, then threshold = mean - 10 LU
    double mean_abs = 0.0;
    for (float ms : above_abs) mean_abs += ms;
    mean_abs /= above_abs.size();
    float rel_threshold_ms = static_cast<float>(mean_abs * std::pow(10.0, -1.0)); // -10 LU

    // Final mean over relative-gated blocks
    double final_mean = 0.0; int count = 0;
    for (float ms : block_ms) {
        if (ms >= rel_threshold_ms) {
            final_mean += ms; ++count;
        }
    }
    if (count == 0) return -70.0f;
    final_mean /= count;

    return final_mean > 1e-10 ? 10.0f * static_cast<float>(std::log10(final_mean)) - 0.691f
                               : -70.0f;
}

// ----------------------------------------------------------------
// Signal-to-Noise Ratio using VAD speech/silence energy
// ----------------------------------------------------------------
inline float compute_snr_db(const std::vector<float>& speech_pcm,
                            const std::vector<float>& noise_pcm) {
    auto rms = [](const std::vector<float>& v) -> double {
        if (v.empty()) return 1e-12;
        double s = 0.0;
        for (float x : v) s += static_cast<double>(x)*x;
        return std::sqrt(s / v.size());
    };
    double s = rms(speech_pcm);
    double n = rms(noise_pcm);
    if (n < 1e-12) n = 1e-12;
    return static_cast<float>(20.0 * std::log10(s / n));
}

// Simplified SNR from a single buffer: estimate noise floor from
// the quietest 20% of 10ms frames.
inline float compute_snr_db_simple(const std::vector<float>& pcm, int sample_rate = 16000) {
    const int frame_size = sample_rate / 100; // 10ms
    if (static_cast<int>(pcm.size()) < frame_size) return 20.0f;

    std::vector<float> frame_energy;
    for (int i = 0; i + frame_size <= static_cast<int>(pcm.size()); i += frame_size) {
        double e = 0.0;
        for (int j = i; j < i + frame_size; ++j)
            e += static_cast<double>(pcm[j])*pcm[j];
        frame_energy.push_back(static_cast<float>(e / frame_size));
    }

    std::sort(frame_energy.begin(), frame_energy.end());
    size_t noise_end = std::max(size_t(1), frame_energy.size() / 5);
    double noise_e = 0.0;
    for (size_t i = 0; i < noise_end; ++i) noise_e += frame_energy[i];
    noise_e /= noise_end;

    double sig_e = 0.0;
    for (float e : frame_energy) sig_e += e;
    sig_e /= frame_energy.size();

    if (noise_e < 1e-12) noise_e = 1e-12;
    return static_cast<float>(10.0 * std::log10(sig_e / noise_e));
}

// ----------------------------------------------------------------
// Harmonics-to-Noise Ratio (HNR) using autocorrelation
// ----------------------------------------------------------------
inline float compute_hnr_db(const std::vector<float>& pcm, float pitch_hz,
                             int sample_rate = 16000) {
    if (pitch_hz < 50.0f || pitch_hz > 600.0f || pcm.empty()) return 15.0f;
    int T0 = static_cast<int>(std::round(sample_rate / pitch_hz));
    if (T0 <= 0 || T0 >= static_cast<int>(pcm.size())) return 15.0f;

    // Autocorrelation at lag 0 and T0
    double r0 = 0.0, rT = 0.0;
    const int N = static_cast<int>(pcm.size()) - T0;
    for (int i = 0; i < N; ++i) {
        r0 += static_cast<double>(pcm[i]) * pcm[i];
        rT += static_cast<double>(pcm[i]) * pcm[i + T0];
    }
    if (r0 < 1e-12) return 15.0f;
    double ratio = rT / r0;
    ratio = std::max(0.0, std::min(0.9999, ratio));
    return static_cast<float>(10.0 * std::log10(ratio / (1.0 - ratio)));
}

// ----------------------------------------------------------------
// RMS energy
// ----------------------------------------------------------------
inline float compute_rms(const std::vector<float>& pcm) {
    if (pcm.empty()) return 0.0f;
    double s = 0.0;
    for (float x : pcm) s += static_cast<double>(x)*x;
    return static_cast<float>(std::sqrt(s / pcm.size()));
}

// ----------------------------------------------------------------
// Spectral centroid of log-mel features (clarity proxy)
// Higher centroid relative to expected speech range → clearer
// ----------------------------------------------------------------
inline float compute_clarity(const std::vector<float>& fbank_frames,
                             int num_bins, int num_frames) {
    if (num_frames <= 0 || num_bins <= 0) return 0.5f;
    // Compute mean spectrum across frames
    std::vector<double> mean_spec(num_bins, 0.0);
    for (int f = 0; f < num_frames; ++f)
        for (int b = 0; b < num_bins; ++b)
            mean_spec[b] += fbank_frames[static_cast<size_t>(f*num_bins+b)];
    for (auto& v : mean_spec) v /= num_frames;

    // Convert from log to linear
    double total = 0.0, weighted = 0.0;
    for (int b = 0; b < num_bins; ++b) {
        double lin = std::exp(mean_spec[b]);
        total    += lin;
        weighted += lin * b;
    }
    if (total < 1e-12) return 0.5f;
    float centroid_bin = static_cast<float>(weighted / total);
    // Normalise to [0,1] relative to 80-bin mel range
    float clarity = std::min(1.0f, centroid_bin / (num_bins * 0.6f));
    return clarity;
}

// ----------------------------------------------------------------
// Energy variability (proxy for speaking dynamics)
// ----------------------------------------------------------------
inline float compute_energy_variability(const std::vector<float>& pcm,
                                        int sample_rate = 16000) {
    const int frame_size = sample_rate / 100;
    if (static_cast<int>(pcm.size()) < frame_size) return 0.0f;
    std::vector<float> energies;
    for (int i = 0; i + frame_size <= static_cast<int>(pcm.size()); i += frame_size) {
        double e = 0.0;
        for (int j = i; j < i + frame_size; ++j)
            e += static_cast<double>(pcm[j])*pcm[j];
        energies.push_back(static_cast<float>(std::sqrt(e / frame_size)));
    }
    double mean = 0.0;
    for (float e : energies) mean += e;
    mean /= energies.size();
    double var = 0.0;
    for (float e : energies) var += (e - mean)*(e - mean);
    return static_cast<float>(std::sqrt(var / energies.size()));
}

} // namespace dsp
} // namespace vp

#endif // VP_LOUDNESS_H
