#ifndef VP_VOICE_ANALYZER_H
#define VP_VOICE_ANALYZER_H

#include <voiceprint/voiceprint_types.h>
#include <string>
#include <memory>
#include <vector>

namespace vp {

class FbankExtractor;
class VoiceActivityDetector;
class OnnxModel;

/**
 * VoiceAnalyzer provides speech analysis beyond speaker identity:
 *   - Gender & age classification
 *   - Emotion recognition (7+1 classes, valence/arousal)
 *   - Anti-spoofing / liveness detection
 *   - Voice quality (MOS, SNR, loudness, clarity)
 *   - Acoustic feature extraction (pitch, speaking rate, stability)
 *   - Voice pleasantness scoring
 *   - Voice state detection (fatigue, health, stress)
 *   - Language & accent identification
 *
 * All models are optional: if the model file is absent the corresponding
 * feature flag silently sets VP_ERROR_MODEL_NOT_AVAILABLE for that sub-result.
 * Features that rely only on DSP (quality metrics, voice features) always work.
 */
class VoiceAnalyzer {
public:
    VoiceAnalyzer();
    ~VoiceAnalyzer();

    /**
     * Initialize the analyzer.
     * @param model_dir   Directory containing ONNX model files.
     * @param feature_flags  Bitmask of VP_FEATURE_* to load.
     * @param ort_env     Shared OrtEnv pointer (created by SpeakerManager or caller).
     * @return true on success (partial success if some models missing is OK).
     */
    bool init(const std::string& model_dir, unsigned int feature_flags, void* ort_env);

    /**
     * Run analysis on PCM audio.
     * @param pcm           Float32 samples @ 16kHz mono, normalized [-1,1].
     * @param sample_count  Number of samples.
     * @param feature_flags Features to compute (subset of what was init'd).
     * @param out           Output result structure (caller-allocated).
     * @return VP_OK or error code.
     */
    int analyze(const float* pcm, int sample_count,
                unsigned int feature_flags, VpAnalysisResult* out);

    /** Anti-spoof check enabled inside vp_verify/identify */
    void set_antispoof_enabled(bool enabled) { antispoof_in_pipeline_ = enabled; }
    bool antispoof_enabled() const           { return antispoof_in_pipeline_; }

    unsigned int loaded_features() const { return loaded_features_; }
    const std::string& last_error() const { return last_error_; }

private:
    // --- Model inference helpers ---
    int analyze_gender_age(const std::vector<float>& fbank,
                           int num_frames, int num_bins,
                           VpGenderResult* g, VpAgeResult* a);

    int analyze_emotion(const std::vector<float>& fbank,
                        int num_frames, int num_bins,
                        VpEmotionResult* out);

    int analyze_antispoof(const std::vector<float>& pcm16k,
                          VpAntiSpoofResult* out);

    int analyze_quality(const std::vector<float>& speech_pcm,
                        const std::vector<float>& noise_pcm,
                        const std::vector<float>& fbank,
                        int num_frames, int num_bins,
                        float pitch_hz,
                        VpQualityResult* out);

    int analyze_voice_features(const std::vector<float>& speech_pcm,
                               const std::vector<float>& fbank,
                               int num_frames, int num_bins,
                               VpVoiceFeatures* out);

    int analyze_pleasantness(const VpQualityResult& q,
                             const VpVoiceFeatures& vf,
                             const VpEmotionResult* emo,
                             VpPleasantnessResult* out);

    int analyze_voice_state(const VpQualityResult& q,
                            const VpVoiceFeatures& vf,
                            const VpEmotionResult* emo,
                            VpVoiceState* out);

    int analyze_language(const std::vector<float>& pcm16k,
                         VpLanguageResult* out);

    // --- DSP helpers ---
    static float estimate_mos_from_metrics(float snr_db, float hnr_db);
    static void  fill_language_info(int lang_idx, VpLanguageResult* out);

public:
    /** Static helpers exposed for DLL convenience functions */
    static const char* emotion_name(int emotion_id);
    static const char* language_name(const char* lang_code);

private:
    // --- Infrastructure ---
    std::unique_ptr<FbankExtractor>      fbank_;
    std::unique_ptr<VoiceActivityDetector> vad_;

    std::unique_ptr<OnnxModel> gender_age_model_;   // VP_FEATURE_GENDER|AGE
    std::unique_ptr<OnnxModel> emotion_model_;      // VP_FEATURE_EMOTION
    std::unique_ptr<OnnxModel> antispoof_model_;    // VP_FEATURE_ANTISPOOF
    std::unique_ptr<OnnxModel> dnsmos_model_;       // VP_FEATURE_QUALITY (MOS part)
    std::unique_ptr<OnnxModel> language_model_;     // VP_FEATURE_LANGUAGE

    void*        ort_env_         = nullptr;
    unsigned int loaded_features_ = 0;
    bool         antispoof_in_pipeline_ = false;
    bool         initialized_    = false;
    std::string  last_error_;

    // Anti-spoof fixed input length: 4s @ 16kHz
    static constexpr int ANTISPOOF_SAMPLES = 64600;
    // Language model mel frames: 3000 (30s Whisper-style)
    static constexpr int LANG_MEL_FRAMES   = 3000;
    static constexpr int LANG_MEL_BINS     = 80;
};

} // namespace vp

#endif // VP_VOICE_ANALYZER_H
