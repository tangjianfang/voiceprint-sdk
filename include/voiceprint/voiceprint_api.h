#ifndef VOICEPRINT_API_H
#define VOICEPRINT_API_H

#include <voiceprint/voiceprint_types.h>

#ifdef _WIN32
    #ifdef VOICEPRINT_EXPORTS
        #define VP_API extern "C" __declspec(dllexport)
    #else
        #define VP_API extern "C" __declspec(dllimport)
    #endif
#else
    #define VP_API extern "C" __attribute__((visibility("default")))
#endif

// Error codes
#define VP_OK                        0
#define VP_ERROR_UNKNOWN            -1
#define VP_ERROR_INVALID_PARAM      -2
#define VP_ERROR_NOT_INIT           -3
#define VP_ERROR_ALREADY_INIT       -4
#define VP_ERROR_MODEL_LOAD         -5
#define VP_ERROR_AUDIO_TOO_SHORT    -6
#define VP_ERROR_AUDIO_INVALID      -7
#define VP_ERROR_SPEAKER_EXISTS     -8
#define VP_ERROR_SPEAKER_NOT_FOUND  -9
#define VP_ERROR_DB_ERROR           -10
#define VP_ERROR_FILE_NOT_FOUND     -11
#define VP_ERROR_BUFFER_TOO_SMALL   -12
#define VP_ERROR_NO_MATCH           -13
#define VP_ERROR_WAV_FORMAT         -14
#define VP_ERROR_INFERENCE          -15
#define VP_ERROR_MODEL_NOT_AVAILABLE -16
#define VP_ERROR_ANALYSIS_FAILED    -17
#define VP_ERROR_DIARIZE_FAILED     -18

/**
 * Initialize the voiceprint SDK.
 * @param model_dir Path to directory containing ONNX model files
 * @param db_path Path to SQLite database file (will be created if not exists)
 * @return VP_OK on success, error code on failure
 */
VP_API int vp_init(const char* model_dir, const char* db_path);

/**
 * Release all resources held by the SDK.
 */
VP_API void vp_release();

/**
 * Enroll a speaker from PCM audio data.
 * @param speaker_id Unique identifier for the speaker
 * @param pcm_data Float32 PCM samples, normalized to [-1.0, 1.0]
 * @param sample_count Number of samples in pcm_data
 * @return VP_OK on success, error code on failure
 */
VP_API int vp_enroll(const char* speaker_id, const float* pcm_data, int sample_count);

/**
 * Enroll a speaker from a WAV file.
 * @param speaker_id Unique identifier for the speaker
 * @param wav_path Path to WAV file
 * @return VP_OK on success, error code on failure
 */
VP_API int vp_enroll_file(const char* speaker_id, const char* wav_path);

/**
 * Remove a speaker from the database.
 * @param speaker_id Speaker to remove
 * @return VP_OK on success, VP_ERROR_SPEAKER_NOT_FOUND if not exists
 */
VP_API int vp_remove_speaker(const char* speaker_id);

/**
 * Identify a speaker from PCM audio (1:N search).
 * @param pcm_data Float32 PCM samples
 * @param sample_count Number of samples
 * @param out_speaker_id Buffer to receive matched speaker ID
 * @param id_buf_size Size of out_speaker_id buffer
 * @param out_score Pointer to receive similarity score
 * @return VP_OK on match, VP_ERROR_NO_MATCH if no match above threshold
 */
VP_API int vp_identify(const float* pcm_data, int sample_count,
                       char* out_speaker_id, int id_buf_size, float* out_score);

/**
 * Verify if audio belongs to a specific speaker (1:1).
 * @param speaker_id Speaker to verify against
 * @param pcm_data Float32 PCM samples
 * @param sample_count Number of samples
 * @param out_score Pointer to receive similarity score
 * @return VP_OK on success, error code on failure
 */
VP_API int vp_verify(const char* speaker_id,
                     const float* pcm_data, int sample_count, float* out_score);

/**
 * Set the similarity threshold for identification/verification.
 * @param threshold Value between 0.0 and 1.0 (default: 0.30)
 * @return VP_OK on success
 */
VP_API int vp_set_threshold(float threshold);

/**
 * Get the number of registered speakers.
 * @return Number of speakers, or negative error code
 */
VP_API int vp_get_speaker_count();

/**
 * Get the last error message.
 * @return Error message string (thread-local, valid until next API call)
 */
VP_API const char* vp_get_last_error();

// ============================================================
// Voice Analysis API
// ============================================================

/**
 * Initialize the voice analyzer with selected feature modules.
 * Must be called after vp_init(). Only models present in model_dir are loaded.
 * @param feature_flags Bitmask of VP_FEATURE_* flags to enable
 * @return VP_OK on success. VP_ERROR_NOT_INIT if vp_init not called first.
 */
VP_API int vp_init_analyzer(unsigned int feature_flags);

/**
 * Analyze voice from PCM data.
 * @param pcm_data Float32 PCM samples @ 16kHz mono, normalized [-1,1]
 * @param sample_count Number of samples
 * @param feature_flags Bitmask of VP_FEATURE_* to compute
 * @param out Pointer to caller-allocated VpAnalysisResult
 * @return VP_OK on success
 */
VP_API int vp_analyze(const float* pcm_data, int sample_count,
                      unsigned int feature_flags, VpAnalysisResult* out);

/**
 * Analyze voice from a WAV file.
 */
VP_API int vp_analyze_file(const char* wav_path,
                           unsigned int feature_flags, VpAnalysisResult* out);

/**
 * Detect gender from PCM audio.
 */
VP_API int vp_get_gender(const float* pcm_data, int sample_count, VpGenderResult* out);
VP_API int vp_get_gender_file(const char* wav_path, VpGenderResult* out);

/**
 * Estimate age from PCM audio.
 */
VP_API int vp_get_age(const float* pcm_data, int sample_count, VpAgeResult* out);
VP_API int vp_get_age_file(const char* wav_path, VpAgeResult* out);

/**
 * Recognize emotion from PCM audio.
 */
VP_API int vp_get_emotion(const float* pcm_data, int sample_count, VpEmotionResult* out);
VP_API int vp_get_emotion_file(const char* wav_path, VpEmotionResult* out);
/**
 * @return Static string name of an emotion ID (e.g. "happy"). Never NULL.
 */
VP_API const char* vp_emotion_name(int emotion_id);

/**
 * Anti-spoofing / liveness detection.
 * @return VP_OK if analysis succeeds. Check out->is_genuine for result.
 */
VP_API int vp_anti_spoof(const float* pcm_data, int sample_count, VpAntiSpoofResult* out);
VP_API int vp_anti_spoof_file(const char* wav_path, VpAntiSpoofResult* out);

/**
 * Enable automatic anti-spoof check inside vp_verify() / vp_identify().
 * @param enabled 1 to enable, 0 to disable (default: 0)
 */
VP_API int vp_set_antispoof_enabled(int enabled);

/**
 * Assess voice quality (MOS, SNR, loudness, clarity).
 */
VP_API int vp_assess_quality(const float* pcm_data, int sample_count, VpQualityResult* out);
VP_API int vp_assess_quality_file(const char* wav_path, VpQualityResult* out);

/**
 * Extract acoustic voice features (pitch, speaking rate, stability, etc.).
 */
VP_API int vp_analyze_voice(const float* pcm_data, int sample_count, VpVoiceFeatures* out);
VP_API int vp_analyze_voice_file(const char* wav_path, VpVoiceFeatures* out);

/**
 * Evaluate voice pleasantness / attractiveness.
 */
VP_API int vp_get_pleasantness(const float* pcm_data, int sample_count, VpPleasantnessResult* out);
VP_API int vp_get_pleasantness_file(const char* wav_path, VpPleasantnessResult* out);

/**
 * Detect voice state (fatigue, health, stress).
 */
VP_API int vp_get_voice_state(const float* pcm_data, int sample_count, VpVoiceState* out);
VP_API int vp_get_voice_state_file(const char* wav_path, VpVoiceState* out);

/**
 * Identify language and accent from speech.
 */
VP_API int vp_detect_language(const float* pcm_data, int sample_count, VpLanguageResult* out);
VP_API int vp_detect_language_file(const char* wav_path, VpLanguageResult* out);
/**
 * @return Human-readable language name for an ISO 639-1 code. Returns code itself if unknown.
 */
VP_API const char* vp_language_name(const char* lang_code);

/**
 * Multi-speaker diarization from PCM audio.
 * @param pcm_data    Float32 PCM @ 16kHz mono
 * @param sample_count Number of samples
 * @param out_segments Caller-allocated array of VpDiarizeSegment
 * @param max_segments Capacity of out_segments array
 * @param out_count   Receives the actual number of segments written
 * @return VP_OK on success, VP_ERROR_DIARIZE_FAILED on failure
 */
VP_API int vp_diarize(const float* pcm_data, int sample_count,
                      VpDiarizeSegment* out_segments, int max_segments, int* out_count);
VP_API int vp_diarize_file(const char* wav_path,
                           VpDiarizeSegment* out_segments, int max_segments, int* out_count);

#endif // VOICEPRINT_API_H
