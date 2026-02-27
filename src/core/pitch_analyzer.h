#pragma once
#ifndef VP_PITCH_ANALYZER_H
#define VP_PITCH_ANALYZER_H

// F0 (fundamental frequency) estimation using YIN algorithm.
// YIN is a classic reliable method for monophonic pitch detection.
// Reference: de Cheveigné & Kawahara (2002), JASA 111(4).

#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace vp {
namespace dsp {

// ----------------------------------------------------------------
// YIN pitch detector
// ----------------------------------------------------------------
struct PitchFrame {
    float f0_hz;         // fundamental frequency, 0 = unvoiced
    float probability;   // voicing probability [0,1]
};

class PitchAnalyzer {
public:
    // sample_rate: 16000, min_f0: 60 Hz, max_f0: 600 Hz
    PitchAnalyzer(int sample_rate = 16000,
                  float min_f0    = 60.0f,
                  float max_f0    = 600.0f,
                  float threshold = 0.15f)
        : sr_(sample_rate), min_f0_(min_f0), max_f0_(max_f0), threshold_(threshold) {
        min_period_ = static_cast<int>(sr_ / max_f0);
        max_period_ = static_cast<int>(sr_ / min_f0);
        frame_size_ = max_period_ * 2;
    }

    // Analyze a full utterance: returns one PitchFrame per 10ms hop.
    std::vector<PitchFrame> analyze(const std::vector<float>& pcm) const {
        const int hop = sr_ / 100; // 10ms
        std::vector<PitchFrame> result;
        if (static_cast<int>(pcm.size()) < frame_size_) return result;

        for (int start = 0; start + frame_size_ <= static_cast<int>(pcm.size()); start += hop) {
            PitchFrame pf = estimate_frame(pcm.data() + start, frame_size_);
            result.push_back(pf);
        }
        return result;
    }

    // Convenience: compute mean F0 (voiced frames only) and variability.
    struct Summary {
        float mean_f0_hz;
        float std_f0_hz;
        float voiced_fraction; // [0,1]
    };

    static Summary summarize(const std::vector<PitchFrame>& frames) {
        Summary s{0, 0, 0};
        if (frames.empty()) return s;
        std::vector<float> voiced;
        for (auto& f : frames)
            if (f.f0_hz > 0.0f) voiced.push_back(f.f0_hz);
        s.voiced_fraction = static_cast<float>(voiced.size()) / frames.size();
        if (voiced.empty()) return s;
        double sum = 0.0;
        for (float v : voiced) sum += v;
        s.mean_f0_hz = static_cast<float>(sum / voiced.size());
        double var = 0.0;
        for (float v : voiced) var += (v - s.mean_f0_hz)*(v - s.mean_f0_hz);
        s.std_f0_hz = static_cast<float>(std::sqrt(var / voiced.size()));
        return s;
    }

private:
    int   sr_, min_period_, max_period_, frame_size_;
    float min_f0_, max_f0_, threshold_;

    PitchFrame estimate_frame(const float* frame, int N) const {
        // Step 1: compute difference function d(tau)
        const int tau_max = std::min(max_period_, N / 2);
        std::vector<double> df(tau_max + 1, 0.0);
        for (int tau = 1; tau <= tau_max; ++tau) {
            for (int j = 0; j + tau < N && j < tau_max * 2; ++j) {
                double diff = frame[j] - frame[j + tau];
                df[tau] += diff * diff;
            }
        }

        // Step 2: cumulative mean normalized difference function (CMNDF)
        std::vector<double> cmndf(tau_max + 1, 1.0);
        cmndf[0] = 1.0;
        double running_sum = 0.0;
        for (int tau = 1; tau <= tau_max; ++tau) {
            running_sum += df[tau];
            cmndf[tau] = (running_sum > 0.0) ? df[tau] * tau / running_sum : 1.0;
        }

        // Step 3: find first minimum below threshold
        int best_tau = -1;
        for (int tau = min_period_; tau <= tau_max; ++tau) {
            if (cmndf[tau] < threshold_) {
                // Parabolic interpolation for sub-sample accuracy
                if (tau > min_period_ && tau < tau_max) {
                    double a = cmndf[tau-1], b = cmndf[tau], c = cmndf[tau+1];
                    double denom = 2.0*(2.0*b - a - c);
                    if (std::abs(denom) > 1e-10)
                        best_tau = tau; // use integer for simplicity
                    else
                        best_tau = tau;
                } else {
                    best_tau = tau;
                }
                break;
            }
        }

        if (best_tau < 0) {
            // Step 4: fallback - take global minimum in valid range
            double min_val = 1e9; int min_t = -1;
            for (int tau = min_period_; tau <= tau_max; ++tau) {
                if (cmndf[tau] < min_val) { min_val = cmndf[tau]; min_t = tau; }
            }
            if (min_val < 0.35 && min_t > 0) best_tau = min_t;
        }

        if (best_tau <= 0) return {0.0f, 0.0f};

        float f0 = static_cast<float>(sr_) / best_tau;
        float prob = static_cast<float>(std::max(0.0, 1.0 - cmndf[best_tau]));
        return {f0, prob};
    }
};

// ----------------------------------------------------------------
// Estimate syllable rate from energy envelope peaks
// ----------------------------------------------------------------
inline float estimate_speaking_rate(const std::vector<float>& pcm,
                                    int sample_rate = 16000) {
    const int frame_size = sample_rate / 100; // 10ms
    int n = static_cast<int>(pcm.size());
    if (n < frame_size) return 0.0f;

    // Compute frame energy
    std::vector<float> energy;
    for (int i = 0; i + frame_size <= n; i += frame_size) {
        double e = 0.0;
        for (int j = i; j < i + frame_size; ++j)
            e += static_cast<double>(pcm[j])*pcm[j];
        energy.push_back(static_cast<float>(std::sqrt(e / frame_size)));
    }

    // Smooth energy (5-frame moving average)
    std::vector<float> smooth(energy.size(), 0.0f);
    for (size_t i = 0; i < energy.size(); ++i) {
        int lo = std::max(0, static_cast<int>(i)-2);
        int hi = std::min(static_cast<int>(energy.size())-1, static_cast<int>(i)+2);
        float s = 0.0f; int cnt = 0;
        for (int k = lo; k <= hi; ++k) { s += energy[k]; ++cnt; }
        smooth[i] = s / cnt;
    }

    // Count peaks (local maxima above mean) → syllable nuclei estimate
    float mean_e = 0.0f;
    for (float e : smooth) mean_e += e;
    mean_e /= smooth.size();

    int peaks = 0;
    const int min_gap = 5; // min 50ms between syllables
    int last_peak = -min_gap;
    for (int i = 1; i + 1 < static_cast<int>(smooth.size()); ++i) {
        if (smooth[i] > smooth[i-1] && smooth[i] > smooth[i+1] &&
            smooth[i] > mean_e * 1.2f && i - last_peak >= min_gap) {
            ++peaks;
            last_peak = i;
        }
    }

    float duration_sec = static_cast<float>(n) / sample_rate;
    return duration_sec > 0.1f ? static_cast<float>(peaks) / duration_sec : 0.0f;
}

// ----------------------------------------------------------------
// Voice stability: jitter (F0 variation) + shimmer (amplitude variation)
// Returns combined stability score [0,1] (1=very stable)
// ----------------------------------------------------------------
inline float compute_voice_stability(const std::vector<PitchFrame>& f0_frames,
                                     const std::vector<float>& pcm,
                                     int sample_rate = 16000) {
    // Jitter: relative period-to-period F0 variation
    std::vector<float> voiced_f0;
    for (auto& f : f0_frames)
        if (f.f0_hz > 0.0f) voiced_f0.push_back(f.f0_hz);

    float jitter = 1.0f;
    if (voiced_f0.size() > 2) {
        double sum_diff = 0.0;
        for (size_t i = 1; i < voiced_f0.size(); ++i)
            sum_diff += std::abs(static_cast<double>(voiced_f0[i]) - voiced_f0[i-1]);
        double mean_f0 = 0.0;
        for (float f : voiced_f0) mean_f0 += f;
        mean_f0 /= voiced_f0.size();
        jitter = static_cast<float>(sum_diff / ((voiced_f0.size()-1) * mean_f0));
    }

    // Shimmer: relative amplitude variation (using RMS per 10ms frame)
    const int hop = sample_rate / 100;
    std::vector<float> frame_rms;
    for (int i = 0; i + hop <= static_cast<int>(pcm.size()); i += hop) {
        double e = 0.0;
        for (int j = i; j < i + hop; ++j) e += static_cast<double>(pcm[j])*pcm[j];
        frame_rms.push_back(static_cast<float>(std::sqrt(e / hop)));
    }
    float shimmer = 1.0f;
    if (frame_rms.size() > 2) {
        double sum_diff = 0.0;
        for (size_t i = 1; i < frame_rms.size(); ++i)
            sum_diff += std::abs(static_cast<double>(frame_rms[i]) - frame_rms[i-1]);
        double mean_amp = 0.0;
        for (float a : frame_rms) mean_amp += a;
        mean_amp /= frame_rms.size();
        if (mean_amp > 1e-6) shimmer = static_cast<float>(sum_diff / ((frame_rms.size()-1) * mean_amp));
    }

    // Convert jitter/shimmer to stability [0,1]
    // Typical speech: jitter ~0.5-2%, shimmer ~3-8%
    float jitter_score   = std::max(0.0f, 1.0f - std::min(1.0f, jitter * 10.0f));
    float shimmer_score  = std::max(0.0f, 1.0f - std::min(1.0f, shimmer * 5.0f));
    return 0.5f * jitter_score + 0.5f * shimmer_score;
}

// ----------------------------------------------------------------
// Breathiness index: ratio of HF noise energy to HF total energy
// in high-frequency band (3-8kHz range in mel spectrum)
// ----------------------------------------------------------------
inline float compute_breathiness(const std::vector<float>& fbank_frames,
                                 int num_bins, int num_frames) {
    if (num_frames <= 0 || num_bins < 40) return 0.3f;
    // High-freq bins ~65-80 in 80-bin mel (rough 3-8kHz at 16kHz)
    int hf_start = (num_bins * 65) / 80;
    double hf_total = 0.0, hf_irregular = 0.0;
    for (int f = 1; f < num_frames; ++f) {
        for (int b = hf_start; b < num_bins; ++b) {
            double cur  = fbank_frames[static_cast<size_t>(f*num_bins+b)];
            double prev = fbank_frames[static_cast<size_t>((f-1)*num_bins+b)];
            hf_total     += std::abs(cur);
            hf_irregular += std::abs(cur - prev);
        }
    }
    if (hf_total < 1e-10) return 0.3f;
    float breath = static_cast<float>(hf_irregular / (hf_total * 2.0));
    return std::min(1.0f, breath);
}

// ----------------------------------------------------------------
// Resonance score: ratio of 1-4kHz energy to total energy
// Bins for 1-4kHz in 80-bin mel at 16kHz: ~bins 40-65 (approx.)
// ----------------------------------------------------------------
inline float compute_resonance_score(const std::vector<float>& fbank_frames,
                                     int num_bins, int num_frames) {
    if (num_frames <= 0 || num_bins < 40) return 0.4f;
    int mid_start = (num_bins * 40) / 80;
    int mid_end   = (num_bins * 65) / 80;
    double mid=0, total=0;
    for (int f = 0; f < num_frames; ++f) {
        for (int b = 0; b < num_bins; ++b) {
            double v = std::exp(fbank_frames[static_cast<size_t>(f*num_bins+b)]);
            total += v;
            if (b >= mid_start && b < mid_end) mid += v;
        }
    }
    if (total < 1e-12) return 0.4f;
    return std::min(1.0f, static_cast<float>(mid / total) * 2.5f); // normalise
}

} // namespace dsp
} // namespace vp

#endif // VP_PITCH_ANALYZER_H
