#ifndef VP_EMBEDDING_EXTRACTOR_H
#define VP_EMBEDDING_EXTRACTOR_H

#include <vector>
#include <string>
#include <memory>

namespace Ort {
    struct Env;
}

namespace vp {

class FbankExtractor;
class OnnxModel;
class VoiceActivityDetector;

class EmbeddingExtractor {
public:
    EmbeddingExtractor();
    ~EmbeddingExtractor();

    // Initialize with model path and ONNX runtime env
    bool init(const std::string& model_dir, void* ort_env);

    // Extract embedding from audio (16kHz, float32, mono)
    // Returns L2-normalized embedding vector
    std::vector<float> extract(const std::vector<float>& audio, int sample_rate = 16000);

    // Extract embedding from WAV file
    std::vector<float> extract_from_file(const std::string& wav_path);

    // Get embedding dimension
    int embedding_dim() const { return embedding_dim_; }

    const std::string& last_error() const { return last_error_; }

private:
    // L2 normalize a vector in-place
    static void l2_normalize(std::vector<float>& vec);

    std::unique_ptr<FbankExtractor> fbank_;
    std::unique_ptr<OnnxModel> speaker_model_;
    std::unique_ptr<VoiceActivityDetector> vad_;

    void* ort_env_ = nullptr;
    int embedding_dim_ = 0;
    bool initialized_ = false;
    std::string last_error_;

    static constexpr float MIN_SPEECH_DURATION = 1.5f; // seconds
};

} // namespace vp

#endif // VP_EMBEDDING_EXTRACTOR_H
