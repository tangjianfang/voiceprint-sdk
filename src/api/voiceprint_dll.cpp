#include <voiceprint/voiceprint_api.h>
#include "manager/speaker_manager.h"
#include "utils/error_codes.h"
#include "utils/logger.h"
#include <memory>
#include <mutex>
#include <cstring>

// Global manager instance
static std::unique_ptr<vp::SpeakerManager> g_manager;
static std::mutex g_init_mutex;

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
