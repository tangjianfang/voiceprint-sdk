#include <voiceprint/voiceprint_api.h>
#include "manager/speaker_manager.h"
#include "manager/diarizer.h"
#include "core/voice_analyzer.h"
#include "core/audio_processor.h"
#include "utils/error_codes.h"
#include "utils/logger.h"
#include <memory>
#include <mutex>
#include <cstring>

// Global manager instance
static std::unique_ptr<vp::SpeakerManager> g_manager;
static std::unique_ptr<vp::VoiceAnalyzer>  g_analyzer;
static std::unique_ptr<vp::Diarizer>       g_diarizer;
static std::mutex g_init_mutex;
static std::string g_model_dir;  // stored on vp_init for re-use by analyzer/diarizer

VP_API int vp_init(const char* model_dir, const char* db_path) {
    std::lock_guard<std::mutex> lock(g_init_mutex);

    if (g_manager) {
        vp::set_last_error(vp::ErrorCode::ALREADY_INIT);
        return VP_ERROR_ALREADY_INIT;
    }

    if (!model_dir || !db_path) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM, "model_dir and db_path must not be null");
        return VP_ERROR_INVALID_PARAM;
    }

    try {
        vp::Logger::instance().init();
        VP_LOG_INFO("Initializing VoicePrint SDK v1.0.0");

        g_model_dir = model_dir;  // store for later use by analyzer/diarizer
        g_manager = std::make_unique<vp::SpeakerManager>();
        if (!g_manager->init(model_dir, db_path)) {
            vp::set_last_error(vp::ErrorCode::MODEL_LOAD, g_manager->last_error());
            g_manager.reset();
            return VP_ERROR_MODEL_LOAD;
        }

        VP_LOG_INFO("VoicePrint SDK initialized successfully");
        return VP_OK;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        g_manager.reset();
        return VP_ERROR_UNKNOWN;
    } catch (...) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN);
        g_manager.reset();
        return VP_ERROR_UNKNOWN;
    }
}

VP_API void vp_release() {
    std::lock_guard<std::mutex> lock(g_init_mutex);

    if (g_diarizer) g_diarizer.reset();
    if (g_analyzer)  g_analyzer.reset();

    if (g_manager) {
        try {
            g_manager->release();
            g_manager.reset();
            VP_LOG_INFO("VoicePrint SDK released");
        } catch (...) {
            g_manager.reset();
        }
    }

    vp::Logger::instance().shutdown();
}

VP_API int vp_enroll(const char* speaker_id, const float* pcm_data, int sample_count) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!speaker_id || !pcm_data || sample_count <= 0) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }

    try {
        int result = g_manager->enroll(speaker_id, pcm_data, sample_count);
        if (result != VP_OK) {
            vp::set_last_error(g_manager->last_error());
        }
        return result;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    } catch (...) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN);
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_enroll_file(const char* speaker_id, const char* wav_path) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!speaker_id || !wav_path) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }

    try {
        int result = g_manager->enroll_file(speaker_id, wav_path);
        if (result != VP_OK) {
            vp::set_last_error(g_manager->last_error());
        }
        return result;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    } catch (...) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN);
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_remove_speaker(const char* speaker_id) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!speaker_id) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }

    try {
        int result = g_manager->remove_speaker(speaker_id);
        if (result != VP_OK) {
            vp::set_last_error(g_manager->last_error());
        }
        return result;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    } catch (...) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN);
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_identify(const float* pcm_data, int sample_count,
                       char* out_speaker_id, int id_buf_size, float* out_score) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!pcm_data || sample_count <= 0 || !out_speaker_id || id_buf_size <= 0 || !out_score) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }

    try {
        std::string speaker_id;
        float score = 0.0f;
        int result = g_manager->identify(pcm_data, sample_count, speaker_id, score);

        *out_score = score;

        if (result == VP_OK) {
            if (static_cast<int>(speaker_id.size()) >= id_buf_size) {
                vp::set_last_error(vp::ErrorCode::BUFFER_TOO_SMALL);
                return VP_ERROR_BUFFER_TOO_SMALL;
            }
            std::strncpy(out_speaker_id, speaker_id.c_str(), id_buf_size - 1);
            out_speaker_id[id_buf_size - 1] = '\0';
        } else {
            out_speaker_id[0] = '\0';
            vp::set_last_error(g_manager->last_error());
        }
        return result;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    } catch (...) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN);
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_verify(const char* speaker_id,
                     const float* pcm_data, int sample_count, float* out_score) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!speaker_id || !pcm_data || sample_count <= 0 || !out_score) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }

    try {
        float score = 0.0f;
        int result = g_manager->verify(speaker_id, pcm_data, sample_count, score);
        *out_score = score;
        if (result != VP_OK) {
            vp::set_last_error(g_manager->last_error());
        }
        return result;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    } catch (...) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN);
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_set_threshold(float threshold) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }

    if (threshold < 0.0f || threshold > 1.0f) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM, "Threshold must be between 0.0 and 1.0");
        return VP_ERROR_INVALID_PARAM;
    }

    g_manager->set_threshold(threshold);
    return VP_OK;
}

VP_API int vp_get_speaker_count() {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }

    return g_manager->get_speaker_count();
}

VP_API const char* vp_get_last_error() {
    return vp::get_last_error();
}

// ============================================================
// Helper: load PCM from file, resampled to 16kHz
// ============================================================
static int load_pcm_from_file(const char* wav_path,
                               std::vector<float>& pcm) {
    vp::AudioProcessor ap;
    int sr = 0;
    if (!ap.read_wav(wav_path, pcm, sr)) {
        vp::set_last_error(vp::ErrorCode::FILE_NOT_FOUND, ap.last_error());
        return VP_ERROR_FILE_NOT_FOUND;
    }
    pcm = ap.normalize(pcm, sr);
    return VP_OK;
}

// ============================================================
// Helper: ensure VoiceAnalyzer is initialized with given flags
// ============================================================
static int ensure_analyzer(unsigned int flags) {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT,
                           "vp_init() must be called before voice analysis");
        return VP_ERROR_NOT_INIT;
    }
    if (!g_analyzer) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT,
                           "vp_init_analyzer() not called");
        return VP_ERROR_NOT_INIT;
    }
    return VP_OK;
}

// ============================================================
// vp_init_analyzer
// ============================================================
VP_API int vp_init_analyzer(unsigned int feature_flags) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    try {
        void* ort_env = vp::SpeakerManager::get_ort_env();

        // Initialize VoiceAnalyzer
        if (!g_analyzer)
            g_analyzer = std::make_unique<vp::VoiceAnalyzer>();
        if (!g_analyzer->init(g_model_dir, feature_flags, ort_env)) {
            vp::set_last_error(vp::ErrorCode::MODEL_LOAD, g_analyzer->last_error());
            g_analyzer.reset();
            return VP_ERROR_MODEL_LOAD;
        }

        // Initialize Diarizer (reuses same models)
        if (!g_diarizer)
            g_diarizer = std::make_unique<vp::Diarizer>();
        if (!g_diarizer->init(g_model_dir, ort_env, g_manager.get())) {
            // Non-fatal: diarizer may fail if models missing, analyzer still usable
            VP_LOG_WARN("Diarizer init failed (feature disabled): {}",
                        g_diarizer->last_error());
            g_diarizer.reset();
        }

        VP_LOG_INFO("VoiceAnalyzer initialized, features=0x{:03x}", feature_flags);
        return VP_OK;
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    }
}

// ============================================================
// vp_analyze / vp_analyze_file
// ============================================================
VP_API int vp_analyze(const float* pcm_data, int sample_count,
                      unsigned int feature_flags, VpAnalysisResult* out) {
    int rc = ensure_analyzer(feature_flags);
    if (rc != VP_OK) return rc;
    if (!pcm_data || sample_count <= 0 || !out) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }
    try {
        return g_analyzer->analyze(pcm_data, sample_count, feature_flags, out);
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_analyze_file(const char* wav_path,
                           unsigned int feature_flags, VpAnalysisResult* out) {
    int rc = ensure_analyzer(feature_flags);
    if (rc != VP_OK) return rc;
    if (!wav_path || !out) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }
    try {
        std::vector<float> pcm;
        int load_rc = load_pcm_from_file(wav_path, pcm);
        if (load_rc != VP_OK) return load_rc;
        return g_analyzer->analyze(pcm.data(), static_cast<int>(pcm.size()),
                                   feature_flags, out);
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    }
}

// ============================================================
// Gender
// ============================================================
VP_API int vp_get_gender(const float* pcm_data, int sample_count, VpGenderResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_GENDER, &result);
    if (rc == VP_OK && out) *out = result.gender;
    return rc;
}

VP_API int vp_get_gender_file(const char* wav_path, VpGenderResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_GENDER, &result);
    if (rc == VP_OK && out) *out = result.gender;
    return rc;
}

// ============================================================
// Age
// ============================================================
VP_API int vp_get_age(const float* pcm_data, int sample_count, VpAgeResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_AGE, &result);
    if (rc == VP_OK && out) *out = result.age;
    return rc;
}

VP_API int vp_get_age_file(const char* wav_path, VpAgeResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_AGE, &result);
    if (rc == VP_OK && out) *out = result.age;
    return rc;
}

// ============================================================
// Emotion
// ============================================================
VP_API int vp_get_emotion(const float* pcm_data, int sample_count, VpEmotionResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_EMOTION, &result);
    if (rc == VP_OK && out) *out = result.emotion;
    return rc;
}

VP_API int vp_get_emotion_file(const char* wav_path, VpEmotionResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_EMOTION, &result);
    if (rc == VP_OK && out) *out = result.emotion;
    return rc;
}

VP_API const char* vp_emotion_name(int emotion_id) {
    return vp::VoiceAnalyzer::emotion_name(emotion_id);
}

// ============================================================
// Anti-spoof
// ============================================================
VP_API int vp_anti_spoof(const float* pcm_data, int sample_count, VpAntiSpoofResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_ANTISPOOF, &result);
    if (rc == VP_OK && out) *out = result.antispoof;
    return rc;
}

VP_API int vp_anti_spoof_file(const char* wav_path, VpAntiSpoofResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_ANTISPOOF, &result);
    if (rc == VP_OK && out) *out = result.antispoof;
    return rc;
}

VP_API int vp_set_antispoof_enabled(int enabled) {
    if (g_analyzer) {
        g_analyzer->set_antispoof_enabled(enabled != 0);
    }
    return VP_OK;
}

// ============================================================
// Quality
// ============================================================
VP_API int vp_assess_quality(const float* pcm_data, int sample_count, VpQualityResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS,
                        &result);
    if (rc == VP_OK && out) *out = result.quality;
    return rc;
}

VP_API int vp_assess_quality_file(const char* wav_path, VpQualityResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS, &result);
    if (rc == VP_OK && out) *out = result.quality;
    return rc;
}

// ============================================================
// Voice features
// ============================================================
VP_API int vp_analyze_voice(const float* pcm_data, int sample_count, VpVoiceFeatures* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_VOICE_FEATS, &result);
    if (rc == VP_OK && out) *out = result.voice_features;
    return rc;
}

VP_API int vp_analyze_voice_file(const char* wav_path, VpVoiceFeatures* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_VOICE_FEATS, &result);
    if (rc == VP_OK && out) *out = result.voice_features;
    return rc;
}

// ============================================================
// Pleasantness
// ============================================================
VP_API int vp_get_pleasantness(const float* pcm_data, int sample_count,
                               VpPleasantnessResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count,
                        VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS |
                        VP_FEATURE_EMOTION  | VP_FEATURE_PLEASANTNESS, &result);
    if (rc == VP_OK && out) *out = result.pleasantness;
    return rc;
}

VP_API int vp_get_pleasantness_file(const char* wav_path, VpPleasantnessResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path,
                             VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS |
                             VP_FEATURE_EMOTION  | VP_FEATURE_PLEASANTNESS, &result);
    if (rc == VP_OK && out) *out = result.pleasantness;
    return rc;
}

// ============================================================
// Voice state
// ============================================================
VP_API int vp_get_voice_state(const float* pcm_data, int sample_count, VpVoiceState* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count,
                        VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS |
                        VP_FEATURE_EMOTION  | VP_FEATURE_VOICE_STATE, &result);
    if (rc == VP_OK && out) *out = result.voice_state;
    return rc;
}

VP_API int vp_get_voice_state_file(const char* wav_path, VpVoiceState* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path,
                             VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS |
                             VP_FEATURE_EMOTION  | VP_FEATURE_VOICE_STATE, &result);
    if (rc == VP_OK && out) *out = result.voice_state;
    return rc;
}

// ============================================================
// Language
// ============================================================
VP_API int vp_detect_language(const float* pcm_data, int sample_count, VpLanguageResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm_data, sample_count, VP_FEATURE_LANGUAGE, &result);
    if (rc == VP_OK && out) *out = result.language;
    return rc;
}

VP_API int vp_detect_language_file(const char* wav_path, VpLanguageResult* out) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav_path, VP_FEATURE_LANGUAGE, &result);
    if (rc == VP_OK && out) *out = result.language;
    return rc;
}

VP_API const char* vp_language_name(const char* lang_code) {
    return vp::VoiceAnalyzer::language_name(lang_code);
}

// ============================================================
// Diarization
// ============================================================
static int ensure_diarizer() {
    if (!g_manager) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!g_diarizer) {
        vp::set_last_error(vp::ErrorCode::NOT_INIT,
                           "vp_init_analyzer() not called (required for diarization)");
        return VP_ERROR_NOT_INIT;
    }
    return VP_OK;
}

VP_API int vp_diarize(const float* pcm_data, int sample_count,
                      VpDiarizeSegment* out_segments, int max_segments, int* out_count) {
    int rc = ensure_diarizer();
    if (rc != VP_OK) return rc;
    if (!pcm_data || sample_count <= 0 || !out_segments || max_segments <= 0 || !out_count) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }
    try {
        return g_diarizer->diarize(pcm_data, sample_count,
                                   out_segments, max_segments, out_count);
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    }
}

VP_API int vp_diarize_file(const char* wav_path,
                           VpDiarizeSegment* out_segments, int max_segments, int* out_count) {
    int rc = ensure_diarizer();
    if (rc != VP_OK) return rc;
    if (!wav_path || !out_segments || max_segments <= 0 || !out_count) {
        vp::set_last_error(vp::ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }
    try {
        std::vector<float> pcm;
        int load_rc = load_pcm_from_file(wav_path, pcm);
        if (load_rc != VP_OK) return load_rc;
        return g_diarizer->diarize(pcm.data(), static_cast<int>(pcm.size()),
                                   out_segments, max_segments, out_count);
    } catch (const std::exception& e) {
        vp::set_last_error(vp::ErrorCode::UNKNOWN, e.what());
        return VP_ERROR_UNKNOWN;
    }
}
