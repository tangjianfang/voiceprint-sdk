#ifndef VP_LOGGER_H
#define VP_LOGGER_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <mutex>

namespace vp {

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void init(const std::string& log_file = "voiceprint.log",
              spdlog::level::level_enum level = spdlog::level::info) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) return;

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(level);

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 5 * 1024 * 1024, 3);
        file_sink->set_level(level);

        logger_ = std::make_shared<spdlog::logger>("voiceprint",
            spdlog::sinks_init_list{console_sink, file_sink});
        logger_->set_level(level);
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        logger_->flush_on(spdlog::level::warn);

        spdlog::register_logger(logger_);
        initialized_ = true;
    }

    std::shared_ptr<spdlog::logger>& get() {
        if (!initialized_) {
            init();
        }
        return logger_;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            spdlog::drop("voiceprint");
            logger_.reset();
            initialized_ = false;
        }
    }

private:
    Logger() = default;
    ~Logger() { shutdown(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::shared_ptr<spdlog::logger> logger_;
    std::mutex mutex_;
    bool initialized_ = false;
};

} // namespace vp

#define VP_LOG_DEBUG(...) vp::Logger::instance().get()->debug(__VA_ARGS__)
#define VP_LOG_INFO(...)  vp::Logger::instance().get()->info(__VA_ARGS__)
#define VP_LOG_WARN(...)  vp::Logger::instance().get()->warn(__VA_ARGS__)
#define VP_LOG_ERROR(...) vp::Logger::instance().get()->error(__VA_ARGS__)

#endif // VP_LOGGER_H
