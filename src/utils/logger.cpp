#include "utils/logger.h"

// Logger implementation is header-only via spdlog
// This .cpp ensures the translation unit exists for the build system
namespace vp {
    // Force instantiation of Logger singleton in this TU
    static auto& logger_ref = Logger::instance();
}
