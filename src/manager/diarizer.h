#ifndef VP_DIARIZER_H
#define VP_DIARIZER_H

#include <voiceprint/voiceprint_types.h>
#include <string>
#include <memory>
#include <vector>

namespace vp {

class EmbeddingExtractor;
class VoiceActivityDetector;
class SpeakerManager;

/**
 * Multi-speaker diarization using VAD + speaker embeddings + agglomerative clustering.
 *
 * Pipeline:
 *   1. VAD → speech segments
 *   2. ECAPA-TDNN → per-segment embedding (segments ≥ 0.5s)
 *   3. Agglomerative clustering (cosine distance) → speaker groups
 *   4. Optional: match clusters against registered speaker database
 */
class Diarizer {
public:
    Diarizer();
    ~Diarizer();

    /**
     * Initialize the diarizer.
     * @param model_dir    Path to the model directory (same as SDK model_dir).
     * @param ort_env      Shared OrtEnv* (cast to void*).
     * @param manager      Optional: SpeakerManager for matching known speakers.
     */
    bool init(const std::string& model_dir, void* ort_env,
              SpeakerManager* manager = nullptr);

    /**
     * Set clustering threshold (cosine distance, default 0.45).
     * Lower → more speakers detected; higher → fewer.
     */
    void set_threshold(float threshold) { threshold_ = threshold; }

    /**
     * Diarize a PCM audio stream.
     * @param pcm            Float32, 16kHz mono.
     * @param sample_count   Number of samples.
     * @param out_segments   Caller-allocated output array.
     * @param max_segments   Capacity of out_segments.
     * @param out_count      Receives actual number of segments written.
     * @return VP_OK or VP_ERROR_DIARIZE_FAILED.
     */
    int diarize(const float* pcm, int sample_count,
                VpDiarizeSegment* out_segments, int max_segments, int* out_count);

    const std::string& last_error() const { return last_error_; }

private:
    std::unique_ptr<EmbeddingExtractor>    extractor_;
    std::unique_ptr<VoiceActivityDetector> vad_;
    SpeakerManager*                        manager_  = nullptr;
    float                                  threshold_ = 0.45f;
    bool                                   initialized_ = false;
    std::string                            last_error_;

    // Minimum segment duration to attempt embedding extraction
    static constexpr float MIN_SEG_DURATION_SEC = 0.5f;
};

} // namespace vp

#endif // VP_DIARIZER_H
