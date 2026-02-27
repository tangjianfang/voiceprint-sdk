#ifndef VP_VAD_H
#define VP_VAD_H

#include <vector>
#include <string>
#include <memory>

namespace Ort {
    struct Env;
    struct Session;
    struct SessionOptions;
    struct MemoryInfo;
    struct Value;
}

namespace vp {

struct SpeechSegment {
    int start_sample;   // Start sample index
    int end_sample;     // End sample index
    float confidence;   // Average confidence
};

class VoiceActivityDetector {
public:
    VoiceActivityDetector();
    ~VoiceActivityDetector();

    // Initialize with ONNX model path
    bool init(const std::string& model_path, void* ort_env);

    // Detect speech segments in audio (16kHz, float32)
    std::vector<SpeechSegment> detect(const std::vector<float>& audio, int sample_rate = 16000);

    // Filter audio to only include speech segments
    std::vector<float> filter_silence(const std::vector<float>& audio, int sample_rate = 16000);

    // Get total speech duration in seconds
    float get_speech_duration(const std::vector<SpeechSegment>& segments, int sample_rate = 16000);

    const std::string& last_error() const { return last_error_; }

private:
    void reset_states();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
    bool initialized_ = false;

    // Model parameters
    static constexpr int WINDOW_SIZE = 512;  // 32ms at 16kHz
    static constexpr float THRESHOLD = 0.5f;
    static constexpr int MIN_SILENCE_DURATION_MS = 300;
    static constexpr int MIN_SPEECH_DURATION_MS = 250;
};

} // namespace vp

#endif // VP_VAD_H
