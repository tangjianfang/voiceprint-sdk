#include "core/embedding_extractor.h"
#include "core/fbank_extractor.h"
#include "core/onnx_model.h"
#include "core/vad.h"
#include "core/audio_processor.h"
#include "utils/logger.h"
#include <onnxruntime_cxx_api.h>
#include <cmath>
#include <numeric>
#include <chrono>

namespace vp {

EmbeddingExtractor::EmbeddingExtractor()
    : fbank_(std::make_unique<FbankExtractor>()),
      speaker_model_(std::make_unique<OnnxModel>()),
      vad_(std::make_unique<VoiceActivityDetector>()) {}

EmbeddingExtractor::~EmbeddingExtractor() = default;

bool EmbeddingExtractor::init(const std::string& model_dir, void* ort_env) {
    ort_env_ = ort_env;
    Ort::Env* env = static_cast<Ort::Env*>(ort_env);

    // Initialize FBank
    fbank_->init(80, 16000, 25.0f, 10.0f);

    // Load VAD model
    std::string vad_path = model_dir + "/silero_vad.onnx";
    if (!vad_->init(vad_path, ort_env)) {
        last_error_ = "Failed to load VAD model: " + vad_->last_error();
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Load speaker embedding model
    std::string model_path = model_dir + "/ecapa_tdnn.onnx";
    if (!speaker_model_->load(model_path, *env)) {
        last_error_ = "Failed to load speaker model: " + speaker_model_->last_error();
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Determine embedding dimension from model output shape
    auto output_shape = speaker_model_->get_output_shape(0);
    if (output_shape.size() >= 2) {
        embedding_dim_ = static_cast<int>(output_shape.back());
    } else if (output_shape.size() == 1) {
        embedding_dim_ = static_cast<int>(output_shape[0]);
    } else {
        // Default ECAPA-TDNN embedding size
        embedding_dim_ = 192;
    }

    VP_LOG_INFO("Embedding extractor initialized: dim={}", embedding_dim_);
    initialized_ = true;
    return true;
}

std::vector<float> EmbeddingExtractor::extract(const std::vector<float>& audio, int sample_rate) {
    if (!initialized_) {
        last_error_ = "Embedding extractor not initialized";
        return {};
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Resample to 16kHz if needed
    std::vector<float> audio_16k;
    if (sample_rate != 16000) {
        audio_16k = AudioProcessor::resample(audio, sample_rate, 16000);
    } else {
        audio_16k = audio;
    }

    // VAD: filter silence (best-effort; fall back to full audio if no speech detected)
    auto speech_audio = vad_->filter_silence(audio_16k, 16000);
    if (speech_audio.empty()) {
        VP_LOG_WARN("VAD detected no speech, using full audio as fallback");
        speech_audio = audio_16k;
    }

    // Check minimum speech duration
    float speech_duration = static_cast<float>(speech_audio.size()) / 16000.0f;
    if (speech_duration < MIN_SPEECH_DURATION) {
        last_error_ = "Speech too short: " + std::to_string(speech_duration) +
                      "s (minimum " + std::to_string(MIN_SPEECH_DURATION) + "s)";
        VP_LOG_WARN(last_error_);
        return {};
    }

    // Extract FBank features
    auto fbank_features = fbank_->extract(speech_audio);
    if (fbank_features.empty()) {
        last_error_ = "FBank feature extraction failed";
        VP_LOG_ERROR(last_error_);
        return {};
    }

    int num_frames = static_cast<int>(fbank_features.size()) / fbank_->num_bins();

    // Prepare input tensor: [1, num_frames, 80]
    std::vector<int64_t> input_shape = {1, num_frames, fbank_->num_bins()};

    // Run speaker model inference
    auto embedding = speaker_model_->run(fbank_features, input_shape);
    if (embedding.empty()) {
        last_error_ = "Speaker model inference failed: " + speaker_model_->last_error();
        VP_LOG_ERROR(last_error_);
        return {};
    }

    // L2 normalize
    l2_normalize(embedding);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    VP_LOG_INFO("Embedding extracted: dim={}, time={}ms, speech_dur={:.2f}s",
                embedding.size(), duration_ms, speech_duration);

    return embedding;
}

std::vector<float> EmbeddingExtractor::extract_from_file(const std::string& wav_path) {
    AudioProcessor processor;
    std::vector<float> samples;
    int sample_rate;

    if (!processor.read_wav(wav_path, samples, sample_rate)) {
        last_error_ = "Failed to read WAV file: " + processor.last_error();
        return {};
    }

    return extract(samples, sample_rate);
}

void EmbeddingExtractor::l2_normalize(std::vector<float>& vec) {
    float norm = 0.0f;
    for (float v : vec) {
        norm += v * v;
    }
    norm = std::sqrt(norm);
    if (norm > 1e-10f) {
        for (float& v : vec) {
            v /= norm;
        }
    }
}

} // namespace vp
