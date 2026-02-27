#include "core/fbank_extractor.h"
#include "utils/logger.h"
#include "kaldi-native-fbank/csrc/online-feature.h"
#include <cmath>
#include <cstring>
#include <numeric>
#include <algorithm>

namespace vp {

FbankExtractor::FbankExtractor() = default;
FbankExtractor::~FbankExtractor() = default;

void FbankExtractor::init(int num_bins, int sample_rate,
                           float frame_length_ms, float frame_shift_ms) {
    num_bins_ = num_bins;
    sample_rate_ = sample_rate;
    frame_length_ms_ = frame_length_ms;
    frame_shift_ms_ = frame_shift_ms;
    frame_length_samples_ = static_cast<int>(frame_length_ms * sample_rate / 1000.0f);
    frame_shift_samples_ = static_cast<int>(frame_shift_ms * sample_rate / 1000.0f);
    initialized_ = true;

    VP_LOG_INFO("FBank initialized: bins={}, rate={}, frame_len={}ms, frame_shift={}ms",
                num_bins_, sample_rate_, frame_length_ms_, frame_shift_ms_);
}

int FbankExtractor::get_num_frames(int num_samples) const {
    if (num_samples < frame_length_samples_) return 0;
    return 1 + (num_samples - frame_length_samples_) / frame_shift_samples_;
}

std::vector<float> FbankExtractor::extract(const std::vector<float>& audio) {
    if (!initialized_) {
        init();
    }

    // Configure kaldi-native-fbank
    knf::FbankOptions opts;
    opts.frame_opts.samp_freq = static_cast<float>(sample_rate_);
    opts.frame_opts.frame_length_ms = frame_length_ms_;
    opts.frame_opts.frame_shift_ms = frame_shift_ms_;
    opts.frame_opts.dither = 0.0f;
    opts.frame_opts.remove_dc_offset = true;
    opts.frame_opts.window_type = "hamming";
    opts.mel_opts.num_bins = num_bins_;
    opts.mel_opts.low_freq = 20.0f;
    opts.mel_opts.high_freq = 0.0f; // Nyquist

    knf::OnlineFbank fbank(opts);

    // Accept waveform
    fbank.AcceptWaveform(sample_rate_, audio.data(), static_cast<int32_t>(audio.size()));
    fbank.InputFinished();

    int num_frames = fbank.NumFramesReady();
    if (num_frames <= 0) {
        VP_LOG_WARN("FBank: no frames extracted from {} samples", audio.size());
        return {};
    }

    // Extract features
    std::vector<float> features(num_frames * num_bins_);
    for (int i = 0; i < num_frames; ++i) {
        const float* frame = fbank.GetFrame(i);
        std::memcpy(features.data() + i * num_bins_, frame, num_bins_ * sizeof(float));
    }

    // Apply CMVN
    apply_cmvn(features, num_frames, num_bins_);

    VP_LOG_DEBUG("FBank: extracted {} frames x {} bins from {} samples",
                 num_frames, num_bins_, audio.size());
    return features;
}

void FbankExtractor::apply_cmvn(std::vector<float>& features, int num_frames, int num_bins) {
    if (num_frames <= 0) return;

    // Compute mean and variance per bin
    std::vector<float> mean(num_bins, 0.0f);
    std::vector<float> var(num_bins, 0.0f);

    for (int i = 0; i < num_frames; ++i) {
        for (int j = 0; j < num_bins; ++j) {
            mean[j] += features[i * num_bins + j];
        }
    }
    for (int j = 0; j < num_bins; ++j) {
        mean[j] /= num_frames;
    }

    for (int i = 0; i < num_frames; ++i) {
        for (int j = 0; j < num_bins; ++j) {
            float diff = features[i * num_bins + j] - mean[j];
            var[j] += diff * diff;
        }
    }
    for (int j = 0; j < num_bins; ++j) {
        var[j] = std::sqrt(var[j] / num_frames + 1e-10f);
    }

    // Normalize: (x - mean) / std
    for (int i = 0; i < num_frames; ++i) {
        for (int j = 0; j < num_bins; ++j) {
            features[i * num_bins + j] = (features[i * num_bins + j] - mean[j]) / var[j];
        }
    }
}

} // namespace vp
