#ifndef VOICEPRINT_API_H
#define VOICEPRINT_API_H

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
#define VP_OK                    0
#define VP_ERROR_UNKNOWN        -1
#define VP_ERROR_INVALID_PARAM  -2
#define VP_ERROR_NOT_INIT       -3
#define VP_ERROR_ALREADY_INIT   -4
#define VP_ERROR_MODEL_LOAD     -5
#define VP_ERROR_AUDIO_TOO_SHORT -6
#define VP_ERROR_AUDIO_INVALID  -7
#define VP_ERROR_SPEAKER_EXISTS -8
#define VP_ERROR_SPEAKER_NOT_FOUND -9
#define VP_ERROR_DB_ERROR       -10
#define VP_ERROR_FILE_NOT_FOUND -11
#define VP_ERROR_BUFFER_TOO_SMALL -12
#define VP_ERROR_NO_MATCH       -13
#define VP_ERROR_WAV_FORMAT     -14
#define VP_ERROR_INFERENCE      -15

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

#endif // VOICEPRINT_API_H
