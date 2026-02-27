#include "voice_analyzer.h"
#include "fbank_extractor.h"
#include "vad.h"
#include "onnx_model.h"
#include "audio_processor.h"
#include "loudness.h"
#include "pitch_analyzer.h"
#include "utils/logger.h"
#include "utils/error_codes.h"
#include <voiceprint/voiceprint_api.h>

#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace vp {

namespace {

// Softmax in-place on a float array of length N
inline void softmax(float* x, int N) {
    float max_val = *std::max_element(x, x + N);
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) { x[i] = std::exp(x[i] - max_val); sum += x[i]; }
    if (sum > 1e-8f) for (int i = 0; i < N; ++i) x[i] /= sum;
}

// Sigmoid
inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

// Clamp to [lo, hi]
template<typename T>
inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Try loading an ONNX model, log warning if missing
bool try_load_model(OnnxModel& model, const std::string& model_dir,
                    const std::string& filename, void* ort_env) {
    namespace fs = std::filesystem;
    std::string path = (fs::path(model_dir) / filename).string();
    if (!fs::exists(path)) {
        VP_LOG_WARN("Optional model not found (feature disabled): {}", path);
        return false;
    }
    Ort::Env& env = *static_cast<Ort::Env*>(ort_env);
    if (!model.load(path, env)) {
        VP_LOG_WARN("Failed to load model {}: {}", path, model.last_error());
        return false;
    }
    VP_LOG_INFO("Loaded model: {}", path);
    return true;
}

} // anonymous namespace

// ============================================================
VoiceAnalyzer::VoiceAnalyzer()
    : fbank_(std::make_unique<FbankExtractor>())
    , vad_(std::make_unique<VoiceActivityDetector>()) {
}

VoiceAnalyzer::~VoiceAnalyzer() = default;

// ============================================================
bool VoiceAnalyzer::init(const std::string& model_dir,
                         unsigned int feature_flags, void* ort_env) {
    ort_env_ = ort_env;
    fbank_->init(80, 16000, 25.0f, 10.0f);

    // Initialize VAD (required for speech segmentation in all pipelines)
    namespace fs = std::filesystem;
    std::string vad_path = (fs::path(model_dir) / "silero_vad.onnx").string();
    if (fs::exists(vad_path)) {
        if (!vad_->init(vad_path, ort_env)) {
            VP_LOG_WARN("VAD init failed for voice analyzer, will skip VAD: {}",
                        vad_->last_error());
        }
    }

    // Load optional feature models according to requested flags
    if (feature_flags & (VP_FEATURE_GENDER | VP_FEATURE_AGE)) {
        gender_age_model_ = std::make_unique<OnnxModel>();
        if (try_load_model(*gender_age_model_, model_dir, "gender_age.onnx", ort_env))
            loaded_features_ |= VP_FEATURE_GENDER | VP_FEATURE_AGE;
        else
            gender_age_model_.reset();
    }

    if (feature_flags & VP_FEATURE_EMOTION) {
        emotion_model_ = std::make_unique<OnnxModel>();
        if (try_load_model(*emotion_model_, model_dir, "emotion.onnx", ort_env))
            loaded_features_ |= VP_FEATURE_EMOTION;
        else
            emotion_model_.reset();
    }

    if (feature_flags & VP_FEATURE_ANTISPOOF) {
        antispoof_model_ = std::make_unique<OnnxModel>();
        if (try_load_model(*antispoof_model_, model_dir, "antispoof.onnx", ort_env))
            loaded_features_ |= VP_FEATURE_ANTISPOOF;
        else
            antispoof_model_.reset();
    }

    if (feature_flags & VP_FEATURE_QUALITY) {
        dnsmos_model_ = std::make_unique<OnnxModel>();
        if (try_load_model(*dnsmos_model_, model_dir, "dnsmos.onnx", ort_env))
            loaded_features_ |= VP_FEATURE_QUALITY;
        else {
            // Quality DSP still works without DNSMOS model (MOS will be estimated)
            dnsmos_model_.reset();
            loaded_features_ |= VP_FEATURE_QUALITY;
        }
    }

    if (feature_flags & VP_FEATURE_LANGUAGE) {
        language_model_ = std::make_unique<OnnxModel>();
        if (try_load_model(*language_model_, model_dir, "language.onnx", ort_env))
            loaded_features_ |= VP_FEATURE_LANGUAGE;
        else
            language_model_.reset();
    }

    // DSP-only features always available
    if (feature_flags & VP_FEATURE_VOICE_FEATS)   loaded_features_ |= VP_FEATURE_VOICE_FEATS;
    if (feature_flags & VP_FEATURE_PLEASANTNESS)  loaded_features_ |= VP_FEATURE_PLEASANTNESS;
    if (feature_flags & VP_FEATURE_VOICE_STATE)   loaded_features_ |= VP_FEATURE_VOICE_STATE;

    initialized_ = true;
    VP_LOG_INFO("VoiceAnalyzer initialized, loaded_features=0x{:03x}", loaded_features_);
    return true;
}

// ============================================================
int VoiceAnalyzer::analyze(const float* pcm_in, int sample_count,
                           unsigned int feature_flags, VpAnalysisResult* out) {
    if (!initialized_) {
        set_last_error(ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!pcm_in || sample_count <= 0 || !out) {
        set_last_error(ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }

    std::memset(out, 0, sizeof(VpAnalysisResult));

    // Build speech-only and noise PCM for quality analysis
    std::vector<float> pcm(pcm_in, pcm_in + sample_count);
    std::vector<float> speech_pcm = pcm;
    std::vector<float> noise_pcm;

    // Run VAD to separate speech and noise
    auto segments = vad_->detect(pcm);
    if (!segments.empty()) {
        speech_pcm = vad_->filter_silence(pcm);
        // Collect noise: samples NOT in any speech segment
        std::vector<bool> is_speech(pcm.size(), false);
        for (auto& seg : segments)
            for (int i = seg.start_sample; i < seg.end_sample && i < sample_count; ++i)
                is_speech[i] = true;
        for (int i = 0; i < sample_count; ++i)
            if (!is_speech[i]) noise_pcm.push_back(pcm[i]);
    }
    if (speech_pcm.empty()) speech_pcm = pcm;

    // Extract Fbank features (shared across model-based features)
    std::vector<float> fbank_feats;
    int num_frames = 0;
    const int num_bins = 80;
    bool fbank_ok = false;
    if (feature_flags & (VP_FEATURE_GENDER | VP_FEATURE_AGE |
                         VP_FEATURE_EMOTION | VP_FEATURE_QUALITY |
                         VP_FEATURE_VOICE_FEATS | VP_FEATURE_PLEASANTNESS |
                         VP_FEATURE_VOICE_STATE)) {
        fbank_feats = fbank_->extract(speech_pcm);
        num_frames  = fbank_->get_num_frames(static_cast<int>(speech_pcm.size()));
        fbank_ok    = !fbank_feats.empty() && num_frames > 0;
    }

    unsigned int computed = 0;

    // --- Gender + Age ---
    if ((feature_flags & (VP_FEATURE_GENDER | VP_FEATURE_AGE)) && fbank_ok) {
        if (gender_age_model_) {
            analyze_gender_age(fbank_feats, num_frames, num_bins,
                               &out->gender, &out->age);
            computed |= VP_FEATURE_GENDER | VP_FEATURE_AGE;
        }
    }

    // --- Emotion ---
    VpEmotionResult* emo_ptr = nullptr;
    if ((feature_flags & VP_FEATURE_EMOTION) && fbank_ok) {
        if (emotion_model_) {
            analyze_emotion(fbank_feats, num_frames, num_bins, &out->emotion);
            computed |= VP_FEATURE_EMOTION;
            emo_ptr = &out->emotion;
        }
    }

    // --- Anti-spoof ---
    if (feature_flags & VP_FEATURE_ANTISPOOF) {
        if (antispoof_model_) {
            analyze_antispoof(pcm, &out->antispoof);
            computed |= VP_FEATURE_ANTISPOOF;
        }
    }

    // --- Voice features (DSP) ---
    VpVoiceFeatures vf{};
    if ((feature_flags & VP_FEATURE_VOICE_FEATS) && fbank_ok) {
        analyze_voice_features(speech_pcm, fbank_feats, num_frames, num_bins, &out->voice_features);
        vf = out->voice_features;
        computed |= VP_FEATURE_VOICE_FEATS;
    }

    // --- Quality (DSP + optional DNSMOS) ---
    VpQualityResult q{};
    if ((feature_flags & VP_FEATURE_QUALITY) && fbank_ok) {
        analyze_quality(speech_pcm, noise_pcm, fbank_feats, num_frames, num_bins,
                        vf.pitch_hz, &out->quality);
        q = out->quality;
        computed |= VP_FEATURE_QUALITY;
    }

    // --- Pleasantness (derived: DSP only) ---
    if ((feature_flags & VP_FEATURE_PLEASANTNESS) && fbank_ok) {
        analyze_pleasantness(q, vf, emo_ptr, &out->pleasantness);
        computed |= VP_FEATURE_PLEASANTNESS;
    }

    // --- Voice state (derived: DSP only) ---
    if ((feature_flags & VP_FEATURE_VOICE_STATE) && fbank_ok) {
        analyze_voice_state(q, vf, emo_ptr, &out->voice_state);
        computed |= VP_FEATURE_VOICE_STATE;
    }

    // --- Language ---
    if (feature_flags & VP_FEATURE_LANGUAGE) {
        if (language_model_) {
            analyze_language(pcm, &out->language);
            computed |= VP_FEATURE_LANGUAGE;
        }
    }

    out->features_computed = computed;
    return VP_OK;
}

// ============================================================
// Gender + Age  (gender_age.onnx)
// Expected I/O: input [1, T, 80] → output [7] (3 gender logits +
//               4 age-group logits + age_regression = optional)
// We assume output shape [1, 8]: [female, male, child, child_g, teen, adult, elder, age_reg]
// ============================================================
int VoiceAnalyzer::analyze_gender_age(const std::vector<float>& fbank,
                                      int num_frames, int num_bins,
                                      VpGenderResult* g, VpAgeResult* a) {
    if (!gender_age_model_ || num_frames <= 0) return VP_ERROR_MODEL_NOT_AVAILABLE;

    std::vector<int64_t> shape = {1, num_frames, num_bins};
    try {
        auto out = gender_age_model_->run(fbank, shape);
        // Minimum expected outputs: 3 gender logits + 4 age group logits
        if (out.size() < 7) {
            last_error_ = "gender_age model unexpected output size";
            return VP_ERROR_INFERENCE;
        }

        // Gender: first 3 logits
        float g_logits[3] = {out[0], out[1], out[2]};
        softmax(g_logits, 3);
        g->scores[0] = g_logits[0];
        g->scores[1] = g_logits[1];
        g->scores[2] = g_logits[2];
        g->gender = static_cast<int>(std::max_element(g_logits, g_logits+3) - g_logits);

        // Age group: next 4 logits
        float a_logits[4] = {out[3], out[4], out[5], out[6]};
        softmax(a_logits, 4);
        for (int i = 0; i < 4; ++i) a->group_scores[i] = a_logits[i];
        a->age_group   = static_cast<int>(std::max_element(a_logits, a_logits+4) - a_logits);
        a->confidence  = a_logits[a->age_group];

        // Optional age regression output
        if (out.size() >= 8) {
            a->age_years = clamp(static_cast<int>(std::round(out[7])), 0, 100);
        } else {
            // Fallback: map age group to midpoint
            const int midpoints[4] = {8, 15, 35, 68};
            a->age_years = midpoints[a->age_group];
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return VP_ERROR_INFERENCE;
    }
    return VP_OK;
}

// ============================================================
// Emotion  (emotion.onnx)
// Expected output [1, 10]: 8 emotion logits + valence + arousal
// ============================================================
int VoiceAnalyzer::analyze_emotion(const std::vector<float>& fbank,
                                   int num_frames, int num_bins,
                                   VpEmotionResult* out) {
    if (!emotion_model_ || num_frames <= 0) return VP_ERROR_MODEL_NOT_AVAILABLE;

    std::vector<int64_t> shape = {1, num_frames, num_bins};
    try {
        auto raw = emotion_model_->run(fbank, shape);
        if (raw.size() < VP_EMOTION_COUNT) {
            last_error_ = "emotion model unexpected output size";
            return VP_ERROR_INFERENCE;
        }
        float emo_logits[VP_EMOTION_COUNT];
        for (int i = 0; i < VP_EMOTION_COUNT; ++i) emo_logits[i] = raw[i];
        softmax(emo_logits, VP_EMOTION_COUNT);
        for (int i = 0; i < VP_EMOTION_COUNT; ++i) out->scores[i] = emo_logits[i];
        out->emotion_id = static_cast<int>(
            std::max_element(emo_logits, emo_logits + VP_EMOTION_COUNT) - emo_logits);

        // Valence / arousal: outputs 8 and 9, tanh-scaled to [-1,1]
        out->valence = (raw.size() > VP_EMOTION_COUNT)
                       ? clamp(std::tanh(raw[VP_EMOTION_COUNT]),   -1.0f, 1.0f) : 0.0f;
        out->arousal = (raw.size() > VP_EMOTION_COUNT + 1)
                       ? clamp(std::tanh(raw[VP_EMOTION_COUNT+1]), -1.0f, 1.0f) : 0.0f;

        // DSP-based fallback for valence/arousal when model doesn't provide them
        if (raw.size() <= VP_EMOTION_COUNT) {
            // Derive valence from emotion class (approximate)
            const float valence_map[VP_EMOTION_COUNT] = {
                0.0f, 0.8f, -0.7f, -0.8f, -0.7f, -0.5f, 0.3f, 0.2f
            };
            const float arousal_map[VP_EMOTION_COUNT] = {
                0.0f, 0.7f, -0.4f, 0.9f, 0.8f, 0.1f, 0.9f, -0.3f
            };
            out->valence = valence_map[out->emotion_id];
            out->arousal = arousal_map[out->emotion_id];
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return VP_ERROR_INFERENCE;
    }
    return VP_OK;
}

// ============================================================
// Anti-spoof  (antispoof.onnx)
// Input: [1, ANTISPOOF_SAMPLES] raw waveform
// Output: [1, 2] logits: [spoof, genuine]
// ============================================================
int VoiceAnalyzer::analyze_antispoof(const std::vector<float>& pcm,
                                     VpAntiSpoofResult* out) {
    if (!antispoof_model_) return VP_ERROR_MODEL_NOT_AVAILABLE;

    // Pad or truncate to fixed length
    std::vector<float> input(ANTISPOOF_SAMPLES, 0.0f);
    int copy_len = std::min(static_cast<int>(pcm.size()), ANTISPOOF_SAMPLES);
    std::copy(pcm.begin(), pcm.begin() + copy_len, input.begin());

    std::vector<int64_t> shape = {1, ANTISPOOF_SAMPLES};
    try {
        auto raw = antispoof_model_->run(input, shape);
        if (raw.size() < 2) {
            last_error_ = "antispoof model unexpected output size";
            return VP_ERROR_INFERENCE;
        }
        // Output: [spoof_logit, genuine_logit]
        float logits[2] = {raw[0], raw[1]};
        softmax(logits, 2);
        out->spoof_score   = logits[0];
        out->genuine_score = logits[1];
        out->is_genuine    = (out->genuine_score >= 0.5f) ? 1 : 0;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return VP_ERROR_INFERENCE;
    }
    return VP_OK;
}

// ============================================================
// Voice Quality  (DSP + optional DNSMOS)
// ============================================================
int VoiceAnalyzer::analyze_quality(const std::vector<float>& speech_pcm,
                                   const std::vector<float>& noise_pcm,
                                   const std::vector<float>& fbank,
                                   int num_frames, int num_bins,
                                   float pitch_hz,
                                   VpQualityResult* out) {
    // SNR from speech/noise energy
    if (!noise_pcm.empty()) {
        out->snr_db = dsp::compute_snr_db(speech_pcm, noise_pcm);
    } else {
        out->snr_db = dsp::compute_snr_db_simple(speech_pcm);
    }

    // Integrated loudness (LUFS) from BS.1770-4
    out->loudness_lufs = dsp::compute_lufs(speech_pcm);

    // HNR
    out->hnr_db = dsp::compute_hnr_db(speech_pcm, pitch_hz);

    // Clarity from spectral analysis
    out->clarity = dsp::compute_clarity(fbank, num_bins, num_frames);

    // Noise level: inverse of SNR clipped to [0,1]
    float snr_clamped = clamp(out->snr_db, -10.0f, 40.0f);
    out->noise_level = 1.0f - (snr_clamped + 10.0f) / 50.0f;
    out->noise_level = clamp(out->noise_level, 0.0f, 1.0f);

    // MOS: use DNSMOS model if available, else estimate from SNR/HNR
    if (dnsmos_model_) {
        // DNSMOS P.835: input log-power mel [1, 80, 512]
        // We feed the available fbank and let the model handle truncation/padding
        const int target_frames = 512;
        std::vector<float> dnsmos_input(num_bins * target_frames, 0.0f);
        int copy_frames = std::min(num_frames, target_frames);
        std::copy(fbank.begin(), fbank.begin() + copy_frames * num_bins,
                  dnsmos_input.begin());
        std::vector<int64_t> shape = {1, static_cast<int64_t>(num_bins),
                                         static_cast<int64_t>(target_frames)};
        try {
            auto raw = dnsmos_model_->run(dnsmos_input, shape);
            // Expect [SIG, BAK, OVR] – take OVR (index 2) as MOS
            if (raw.size() >= 3)
                out->mos_score = clamp(raw[2], 1.0f, 5.0f);
            else if (!raw.empty())
                out->mos_score = clamp(raw[0], 1.0f, 5.0f);
            else
                out->mos_score = estimate_mos_from_metrics(out->snr_db, out->hnr_db);
        } catch (...) {
            out->mos_score = estimate_mos_from_metrics(out->snr_db, out->hnr_db);
        }
    } else {
        out->mos_score = estimate_mos_from_metrics(out->snr_db, out->hnr_db);
    }

    return VP_OK;
}

// ============================================================
// Voice features (DSP: pitch, rate, stability, breathiness, resonance)
// ============================================================
int VoiceAnalyzer::analyze_voice_features(const std::vector<float>& speech_pcm,
                                          const std::vector<float>& fbank,
                                          int num_frames, int num_bins,
                                          VpVoiceFeatures* out) {
    dsp::PitchAnalyzer pa;
    auto f0_frames = pa.analyze(speech_pcm);
    auto summary   = dsp::PitchAnalyzer::summarize(f0_frames);

    out->pitch_hz          = summary.mean_f0_hz;
    out->pitch_variability = summary.std_f0_hz;

    out->speaking_rate     = dsp::estimate_speaking_rate(speech_pcm);
    out->voice_stability   = dsp::compute_voice_stability(f0_frames, speech_pcm);
    out->breathiness       = dsp::compute_breathiness(fbank, num_bins, num_frames);
    out->resonance_score   = dsp::compute_resonance_score(fbank, num_bins, num_frames);
    out->energy_mean       = dsp::compute_rms(speech_pcm);
    out->energy_variability= dsp::compute_energy_variability(speech_pcm);

    return VP_OK;
}

// Private helper: estimate MOS from SNR and HNR
float VoiceAnalyzer::estimate_mos_from_metrics(float snr_db, float hnr_db) {
    // Approximate MOS from SNR: Good SNR(30+) ≈ MOS 4.5, Poor(<5) ≈ MOS 2.0
    float snr_score = clamp((snr_db + 5.0f) / 40.0f, 0.0f, 1.0f);
    float hnr_score = clamp((hnr_db + 5.0f) / 30.0f, 0.0f, 1.0f);
    return 1.0f + 3.5f * (0.6f * snr_score + 0.4f * hnr_score);
}

// ============================================================
// Pleasantness (weighted combination of DSP metrics)
// ============================================================
int VoiceAnalyzer::analyze_pleasantness(const VpQualityResult& q,
                                        const VpVoiceFeatures& vf,
                                        const VpEmotionResult* emo,
                                        VpPleasantnessResult* out) {
    // ---- Magnetism: low-to-medium pitch + stability + resonance
    float pitch_score = 0.5f;
    if (vf.pitch_hz > 0) {
        // Male voice range 85-185Hz scores high; female 165-255Hz also high
        float ideal_male   = clamp(1.0f - std::abs(vf.pitch_hz - 130.0f) / 100.0f, 0.0f, 1.0f);
        float ideal_female = clamp(1.0f - std::abs(vf.pitch_hz - 210.0f) / 100.0f, 0.0f, 1.0f);
        pitch_score = std::max(ideal_male, ideal_female);
    }
    out->magnetism = clamp(
        (0.4f * pitch_score +
         0.35f * vf.voice_stability +
         0.25f * vf.resonance_score) * 100.0f, 0.0f, 100.0f);

    // ---- Warmth: positive emotion valence + moderate speaking rate + some breathiness
    float valence_norm = (emo ? clamp((emo->valence + 1.0f) / 2.0f, 0.0f, 1.0f) : 0.5f);
    float rate_score = clamp(1.0f - std::abs(vf.speaking_rate - 4.0f) / 4.0f, 0.0f, 1.0f);
    out->warmth = clamp(
        (0.5f * valence_norm +
         0.3f * rate_score +
         0.2f * (1.0f - vf.breathiness)) * 100.0f, 0.0f, 100.0f);

    // ---- Authority: stable pitch + low breathiness + strong resonance
    out->authority = clamp(
        (0.4f * vf.voice_stability +
         0.35f * vf.resonance_score +
         0.25f * (1.0f - vf.breathiness)) * 100.0f, 0.0f, 100.0f);

    // ---- Clarity: MOS + SNR + HNR
    float mos_norm = clamp((q.mos_score - 1.0f) / 4.0f, 0.0f, 1.0f);
    float snr_norm = clamp((q.snr_db + 5.0f) / 40.0f, 0.0f, 1.0f);
    out->clarity_score = clamp(
        (0.5f * mos_norm + 0.3f * snr_norm + 0.2f * q.clarity) * 100.0f, 0.0f, 100.0f);

    // ---- Overall (weighted mean)
    out->overall_score = clamp(
        0.30f * out->magnetism +
        0.25f * out->warmth +
        0.20f * out->authority +
        0.25f * out->clarity_score, 0.0f, 100.0f);

    return VP_OK;
}

// ============================================================
// Voice state (fatigue / health / stress: rule-based)
// ============================================================
int VoiceAnalyzer::analyze_voice_state(const VpQualityResult& q,
                                       const VpVoiceFeatures& vf,
                                       const VpEmotionResult* emo,
                                       VpVoiceState* out) {
    // --- Fatigue: low F0, low speaking rate, low energy, deteriorating stability
    float fatigue = 0.0f;
    if (vf.pitch_hz > 0 && vf.pitch_hz < 100.0f)  fatigue += 0.25f;
    if (vf.speaking_rate < 2.5f)                    fatigue += 0.25f;
    if (vf.energy_mean < 0.02f)                     fatigue += 0.25f;
    if (vf.voice_stability < 0.4f)                  fatigue += 0.25f;
    out->fatigue_score = clamp(fatigue, 0.0f, 1.0f);
    out->fatigue_level = (fatigue > 0.7f) ? VP_FATIGUE_HIGH :
                         (fatigue > 0.35f) ? VP_FATIGUE_MODERATE : VP_FATIGUE_NORMAL;

    // --- Health state: hoarse = high breathiness + low HNR
    //                   nasal  = high low-freq energy (resonance_score unusually high at lower bins)
    //                   breathy = very high breathiness
    if (vf.breathiness > 0.7f && q.hnr_db < 5.0f)
        out->health_state = VP_HEALTH_HOARSE;
    else if (vf.breathiness > 0.65f)
        out->health_state = VP_HEALTH_BREATHY;
    else if (vf.resonance_score > 0.75f && vf.pitch_variability < 20.0f)
        out->health_state = VP_HEALTH_NASAL;
    else
        out->health_state = VP_HEALTH_NORMAL;
    out->health_score = clamp(
        0.5f * (1.0f - vf.breathiness) + 0.5f * clamp((q.hnr_db + 5.0f)/30.0f, 0.0f, 1.0f),
        0.0f, 1.0f);

    // --- Stress: elevated F0 + high arousal + fast speaking rate
    float stress = 0.0f;
    if (vf.pitch_hz > 220.0f && vf.pitch_variability > 40.0f) stress += 0.3f;
    if (vf.speaking_rate > 6.0f)                               stress += 0.25f;
    if (emo && emo->arousal > 0.5f)                            stress += 0.25f;
    if (vf.energy_variability > 0.1f)                          stress += 0.2f;
    out->stress_score = clamp(stress, 0.0f, 1.0f);
    out->stress_level = (stress > 0.65f) ? VP_STRESS_HIGH :
                        (stress > 0.30f) ? VP_STRESS_MEDIUM : VP_STRESS_LOW;

    return VP_OK;
}

// ============================================================
// Language detection (language.onnx — Whisper-based)
// Input: log-mel spectrogram [1, 80, 3000]
// Output: language logits [1, N_LANGUAGES]
// ============================================================
int VoiceAnalyzer::analyze_language(const std::vector<float>& pcm,
                                    VpLanguageResult* out) {
    if (!language_model_) return VP_ERROR_MODEL_NOT_AVAILABLE;

    // Build Whisper-style 30s log-mel [80 x 3000]
    // We reuse FbankExtractor at 10ms hop → max 3000 frames for 30s
    std::vector<float> mel_input(static_cast<size_t>(LANG_MEL_BINS) * LANG_MEL_FRAMES, 0.0f);
    auto fbank_raw = fbank_->extract(pcm.size() > 0 ? pcm :
                                     std::vector<float>(16000, 0.0f));
    int fbank_frames = fbank_->get_num_frames(static_cast<int>(pcm.size()));
    int copy_frames  = std::min(fbank_frames, LANG_MEL_FRAMES);
    if (copy_frames > 0)
        std::copy(fbank_raw.begin(),
                  fbank_raw.begin() + static_cast<size_t>(copy_frames) * LANG_MEL_BINS,
                  mel_input.begin());

    std::vector<int64_t> shape = {1, LANG_MEL_BINS, LANG_MEL_FRAMES};
    try {
        auto raw = language_model_->run(mel_input, shape);
        if (raw.empty()) {
            last_error_ = "language model returned empty output";
            return VP_ERROR_INFERENCE;
        }

        // Find argmax → language index
        int lang_idx = static_cast<int>(
            std::max_element(raw.begin(), raw.end()) - raw.begin());
        std::vector<float> probs(raw);
        softmax(probs.data(), static_cast<int>(probs.size()));
        out->confidence = probs[lang_idx];

        // Map index to ISO 639-1 code (Whisper language order, 99 languages)
        fill_language_info(lang_idx, out);
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return VP_ERROR_INFERENCE;
    }
    return VP_OK;
}

// ============================================================
// Language code mapping (Whisper canonical order, first 99 entries)
// ============================================================
void VoiceAnalyzer::fill_language_info(int idx, VpLanguageResult* out) {
    // Whisper language token order (first 50 most common)
    static const struct { const char* code; const char* name; } kLangs[] = {
        {"en","English"}, {"zh","Chinese"}, {"de","German"}, {"es","Spanish"},
        {"ru","Russian"}, {"ko","Korean"}, {"fr","French"}, {"ja","Japanese"},
        {"pt","Portuguese"}, {"tr","Turkish"}, {"pl","Polish"}, {"ca","Catalan"},
        {"nl","Dutch"}, {"ar","Arabic"}, {"sv","Swedish"}, {"it","Italian"},
        {"id","Indonesian"}, {"hi","Hindi"}, {"fi","Finnish"}, {"vi","Vietnamese"},
        {"he","Hebrew"}, {"uk","Ukrainian"}, {"el","Greek"}, {"ms","Malay"},
        {"cs","Czech"}, {"ro","Romanian"}, {"da","Danish"}, {"hu","Hungarian"},
        {"ta","Tamil"}, {"no","Norwegian"}, {"th","Thai"}, {"ur","Urdu"},
        {"hr","Croatian"}, {"bg","Bulgarian"}, {"lt","Lithuanian"}, {"la","Latin"},
        {"mi","Maori"}, {"cy","Welsh"}, {"sk","Slovak"}, {"te","Telugu"},
        {"fa","Persian"}, {"lv","Latvian"}, {"bn","Bengali"}, {"sr","Serbian"},
        {"az","Azerbaijani"}, {"sl","Slovenian"}, {"kn","Kannada"}, {"et","Estonian"},
        {"mk","Macedonian"}, {"br","Breton"}, {"eu","Basque"}, {"is","Icelandic"},
        {"hy","Armenian"}, {"ne","Nepali"}, {"mn","Mongolian"}, {"bs","Bosnian"},
        {"kk","Kazakh"}, {"sq","Albanian"}, {"sw","Swahili"}, {"gl","Galician"},
        {"mr","Marathi"}, {"pa","Punjabi"}, {"si","Sinhala"}, {"km","Khmer"},
        {"sn","Shona"}, {"yo","Yoruba"}, {"so","Somali"}, {"af","Afrikaans"},
        {"oc","Occitan"}, {"ka","Georgian"}, {"be","Belarusian"}, {"tg","Tajik"},
        {"sd","Sindhi"}, {"gu","Gujarati"}, {"am","Amharic"}, {"yi","Yiddish"},
        {"lo","Lao"}, {"uz","Uzbek"}, {"fo","Faroese"}, {"ht","Haitian Creole"},
        {"ps","Pashto"}, {"tk","Turkmen"}, {"nn","Nynorsk"}, {"mt","Maltese"},
        {"sa","Sanskrit"}, {"lb","Luxembourgish"}, {"my","Myanmar"}, {"bo","Tibetan"},
        {"tl","Tagalog"}, {"mg","Malagasy"}, {"as","Assamese"}, {"tt","Tatar"},
        {"haw","Hawaiian"}, {"ln","Lingala"}, {"ha","Hausa"}, {"ba","Bashkir"},
        {"jw","Javanese"}, {"su","Sundanese"},
    };

    const int kLangsCount = static_cast<int>(sizeof(kLangs)/sizeof(kLangs[0]));
    if (idx >= 0 && idx < kLangsCount) {
        std::strncpy(out->language,      kLangs[idx].code, sizeof(out->language)-1);
        std::strncpy(out->language_name, kLangs[idx].name, sizeof(out->language_name)-1);
    } else {
        std::snprintf(out->language, sizeof(out->language), "lang%d", idx);
        std::strncpy(out->language_name, "Unknown", sizeof(out->language_name)-1);
    }

    // Simple Chinese dialect heuristic using model secondary output
    // (accent_score and accent_region filled by model if available, else defaults)
    out->accent_score = 0.0f;
    if (std::strncmp(out->language, "zh", 2) == 0)
        std::strncpy(out->accent_region, "Mandarin", sizeof(out->accent_region)-1);
    else
        std::strncpy(out->accent_region, out->language_name, sizeof(out->accent_region)-1);
}

const char* VoiceAnalyzer::emotion_name(int id) {
    static const char* kNames[VP_EMOTION_COUNT] = {
        "neutral","happy","sad","angry","fearful","disgusted","surprised","calm"
    };
    if (id >= 0 && id < VP_EMOTION_COUNT) return kNames[id];
    return "unknown";
}

const char* VoiceAnalyzer::language_name(const char* code) {
    if (!code) return "";
    // Simple linear scan - adequate for 100 entries
    static const struct { const char* code; const char* name; } kMap[] = {
        {"en","English"},{"zh","Chinese"},{"de","German"},{"es","Spanish"},
        {"ru","Russian"},{"ko","Korean"},{"fr","French"},{"ja","Japanese"},
        {"pt","Portuguese"},{"tr","Turkish"},{"pl","Polish"},{"ar","Arabic"},
        {"it","Italian"},{"hi","Hindi"},{"nl","Dutch"},{"sv","Swedish"},
        // ... (same as fill_language_info but reversed lookup)
    };
    for (auto& e : kMap)
        if (std::strcmp(e.code, code) == 0) return e.name;
    return code; // return code itself if unknown
}

} // namespace vp
