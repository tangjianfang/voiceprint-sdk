#include "core/vad.h"
#include "utils/logger.h"
#include <onnxruntime_cxx_api.h>
#include <cstring>
#include <algorithm>
#include <numeric>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace vp {

struct VoiceActivityDetector::Impl {
    Ort::Session* session = nullptr;
    Ort::SessionOptions session_options;
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Combined hidden state for Silero VAD: [2, 1, 128]
    std::vector<float> state;
    int64_t sr_tensor_val = 16000;

    static constexpr int STATE_SIZE = 2 * 1 * 128;

    Impl() {
        state.resize(STATE_SIZE, 0.0f);
    }

    ~Impl() {
        delete session;
    }
};

VoiceActivityDetector::VoiceActivityDetector() : impl_(std::make_unique<Impl>()) {}

VoiceActivityDetector::~VoiceActivityDetector() = default;

bool VoiceActivityDetector::init(const std::string& model_path, void* ort_env) {
    try {
        Ort::Env* env = static_cast<Ort::Env*>(ort_env);

        impl_->session_options.SetIntraOpNumThreads(1);
        impl_->session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Convert path to wide string for Windows (UTF-8 safe)
        int wlen = MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, &wpath[0], wlen);
        impl_->session = new Ort::Session(*env, wpath.c_str(), impl_->session_options);

        reset_states();
        initialized_ = true;
        VP_LOG_INFO("VAD model loaded successfully from: {}", model_path);
        return true;
    } catch (const Ort::Exception& e) {
        last_error_ = std::string("Failed to load VAD model: ") + e.what();
        VP_LOG_ERROR(last_error_);
        return false;
    }
}

void VoiceActivityDetector::reset_states() {
    std::fill(impl_->state.begin(), impl_->state.end(), 0.0f);
}

std::vector<SpeechSegment> VoiceActivityDetector::detect(const std::vector<float>& audio,
                                                          int sample_rate) {
    if (!initialized_) {
        last_error_ = "VAD not initialized";
        return {};
    }

    reset_states();

    std::vector<SpeechSegment> segments;
    const int window_size = WINDOW_SIZE;
    const int min_silence_samples = MIN_SILENCE_DURATION_MS * sample_rate / 1000;
    const int min_speech_samples = MIN_SPEECH_DURATION_MS * sample_rate / 1000;

    bool in_speech = false;
    int speech_start = 0;
    int silence_counter = 0;
    float speech_confidence_sum = 0.0f;
    int speech_frame_count = 0;

    // Input/output names (Silero VAD v5 format)
    const char* input_names[] = {"input", "state", "sr"};
    const char* output_names[] = {"output", "stateN"};

    for (size_t offset = 0; offset + window_size <= audio.size(); offset += window_size) {
        // Prepare input tensor: [1, window_size]
        std::vector<float> window(audio.begin() + offset, audio.begin() + offset + window_size);
        int64_t input_shape[] = {1, window_size};
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            impl_->memory_info, window.data(), window.size(), input_shape, 2);

        // State tensor: [2, 1, 128]
        int64_t state_shape[] = {2, 1, 128};
        Ort::Value state_tensor = Ort::Value::CreateTensor<float>(
            impl_->memory_info, impl_->state.data(), impl_->state.size(), state_shape, 3);

        // Sample rate tensor: scalar
        int64_t sr_shape[] = {1};
        Ort::Value sr_tensor = Ort::Value::CreateTensor<int64_t>(
            impl_->memory_info, &impl_->sr_tensor_val, 1, sr_shape, 1);

        // Run inference
        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(input_tensor));
        inputs.push_back(std::move(state_tensor));
        inputs.push_back(std::move(sr_tensor));

        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr},
                                           input_names, inputs.data(), inputs.size(),
                                           output_names, 2);

        float prob = outputs[0].GetTensorData<float>()[0];

        // Update hidden state
        const float* new_state = outputs[1].GetTensorData<float>();
        std::memcpy(impl_->state.data(), new_state, impl_->state.size() * sizeof(float));

        int current_sample = static_cast<int>(offset);

        if (prob >= THRESHOLD) {
            if (!in_speech) {
                speech_start = current_sample;
                in_speech = true;
                speech_confidence_sum = 0.0f;
                speech_frame_count = 0;
            }
            silence_counter = 0;
            speech_confidence_sum += prob;
            speech_frame_count++;
        } else {
            if (in_speech) {
                silence_counter += window_size;
                if (silence_counter >= min_silence_samples) {
                    int speech_end = current_sample - silence_counter + window_size;
                    if (speech_end - speech_start >= min_speech_samples) {
                        SpeechSegment seg;
                        seg.start_sample = speech_start;
                        seg.end_sample = speech_end;
                        seg.confidence = speech_frame_count > 0 ?
                            speech_confidence_sum / speech_frame_count : 0.0f;
                        segments.push_back(seg);
                    }
                    in_speech = false;
                    silence_counter = 0;
                }
            }
        }
    }

    // Handle last segment
    if (in_speech) {
        int speech_end = static_cast<int>(audio.size());
        if (speech_end - speech_start >= min_speech_samples) {
            SpeechSegment seg;
            seg.start_sample = speech_start;
            seg.end_sample = speech_end;
            seg.confidence = speech_frame_count > 0 ?
                speech_confidence_sum / speech_frame_count : 0.0f;
            segments.push_back(seg);
        }
    }

    // Merge adjacent segments (gap < MIN_SILENCE_DURATION_MS)
    if (segments.size() > 1) {
        std::vector<SpeechSegment> merged;
        merged.push_back(segments[0]);
        for (size_t i = 1; i < segments.size(); ++i) {
            int gap = segments[i].start_sample - merged.back().end_sample;
            if (gap < min_silence_samples) {
                merged.back().end_sample = segments[i].end_sample;
                merged.back().confidence = (merged.back().confidence + segments[i].confidence) / 2.0f;
            } else {
                merged.push_back(segments[i]);
            }
        }
        segments = std::move(merged);
    }

    VP_LOG_INFO("VAD detected {} speech segments", segments.size());
    return segments;
}

std::vector<float> VoiceActivityDetector::filter_silence(const std::vector<float>& audio,
                                                          int sample_rate) {
    auto segments = detect(audio, sample_rate);
    if (segments.empty()) {
        return {};
    }

    std::vector<float> filtered;
    for (const auto& seg : segments) {
        int start = std::max(0, seg.start_sample);
        int end = std::min(static_cast<int>(audio.size()), seg.end_sample);
        filtered.insert(filtered.end(), audio.begin() + start, audio.begin() + end);
    }

    VP_LOG_INFO("VAD: input {} samples -> output {} samples (filtered {}%)",
                audio.size(), filtered.size(),
                100 - (filtered.size() * 100 / std::max(audio.size(), size_t(1))));
    return filtered;
}

float VoiceActivityDetector::get_speech_duration(const std::vector<SpeechSegment>& segments,
                                                  int sample_rate) {
    float total = 0.0f;
    for (const auto& seg : segments) {
        total += static_cast<float>(seg.end_sample - seg.start_sample) / sample_rate;
    }
    return total;
}

} // namespace vp
