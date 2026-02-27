#ifndef VP_ERROR_CODES_H
#define VP_ERROR_CODES_H

#include <string>

namespace vp {

enum class ErrorCode {
    OK = 0,
    UNKNOWN = -1,
    INVALID_PARAM = -2,
    NOT_INIT = -3,
    ALREADY_INIT = -4,
    MODEL_LOAD = -5,
    AUDIO_TOO_SHORT = -6,
    AUDIO_INVALID = -7,
    SPEAKER_EXISTS = -8,
    SPEAKER_NOT_FOUND = -9,
    DB_ERROR = -10,
    FILE_NOT_FOUND = -11,
    BUFFER_TOO_SMALL = -12,
    NO_MATCH = -13,
    WAV_FORMAT = -14,
    INFERENCE = -15
};

inline const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK: return "Success";
        case ErrorCode::UNKNOWN: return "Unknown error";
        case ErrorCode::INVALID_PARAM: return "Invalid parameter";
        case ErrorCode::NOT_INIT: return "SDK not initialized";
        case ErrorCode::ALREADY_INIT: return "SDK already initialized";
        case ErrorCode::MODEL_LOAD: return "Failed to load model";
        case ErrorCode::AUDIO_TOO_SHORT: return "Audio too short (minimum 1.5s after VAD)";
        case ErrorCode::AUDIO_INVALID: return "Invalid audio data";
        case ErrorCode::SPEAKER_EXISTS: return "Speaker already exists";
        case ErrorCode::SPEAKER_NOT_FOUND: return "Speaker not found";
        case ErrorCode::DB_ERROR: return "Database error";
        case ErrorCode::FILE_NOT_FOUND: return "File not found";
        case ErrorCode::BUFFER_TOO_SMALL: return "Output buffer too small";
        case ErrorCode::NO_MATCH: return "No matching speaker found";
        case ErrorCode::WAV_FORMAT: return "Invalid WAV format";
        case ErrorCode::INFERENCE: return "Model inference error";
        default: return "Unknown error code";
    }
}

// Thread-local error message storage
inline thread_local std::string g_last_error;

inline void set_last_error(const std::string& msg) {
    g_last_error = msg;
}

inline void set_last_error(ErrorCode code) {
    g_last_error = error_code_to_string(code);
}

inline void set_last_error(ErrorCode code, const std::string& detail) {
    g_last_error = std::string(error_code_to_string(code)) + ": " + detail;
}

inline const char* get_last_error() {
    return g_last_error.c_str();
}

} // namespace vp

#endif // VP_ERROR_CODES_H
