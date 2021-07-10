#include "log.h"
#include "../console/sink.h"
#include "error_code.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unordered_map>
#include <string_view>
#include <vector>

namespace syncspirit::utils {

spdlog::level::level_enum get_log_level(const std::string &log_level) noexcept {
    using namespace spdlog::level;
    level_enum value = info;
    if (log_level == "trace")
        value = trace;
    if (log_level == "debug")
        value = debug;
    if (log_level == "info")
        value = info;
    if (log_level == "warn")
        value = warn;
    if (log_level == "error")
        value = err;
    if (log_level == "crit")
        value = critical;
    if (log_level == "off")
        value = off;
    return value;
}

using sink_option_t = outcome::result<spdlog::sink_ptr>;

static sink_option_t make_sink(std::string_view name, std::string &prompt, std::mutex &mutex) noexcept {
    if (name == "interactive") {
        return std::make_shared<console::sink_t>(stdout, spdlog::color_mode::automatic, mutex, prompt);
    } else if (name == "stdout") {
        return std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    } else if (name == "stderr") {
        return std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    }
    return make_error_code(error_code_t::unknown_sink);
}

void set_default(const std::string &level, std::string &prompt, std::mutex &mutex) noexcept {
    auto sink = make_sink("interactive", prompt, mutex);
    auto logger = std::make_shared<spdlog::logger>("default", sink.value());
    logger->set_level(get_log_level(level));
    spdlog::set_default_logger(logger);
}

outcome::result<void> init_loggers(const config::log_configs_t &configs, std::string &prompt, std::mutex &mutex,
                                   bool overwrite_default) noexcept {
    using sink_map_t = std::unordered_map<std::string, spdlog::sink_ptr>;
    using logger_map_t = std::unordered_map<std::string, std::shared_ptr<spdlog::logger>>;

    sink_map_t sink_map;
    for (auto &cfg : configs) {
        for (auto &sink : cfg.sinks) {
            auto sink_option = make_sink(sink, prompt, mutex);
            if (!sink_option) {
                return sink_option.error();
            }
            sink_map[sink] = sink_option.value();
        }
    }

    logger_map_t logger_map;
    for (auto &cfg : configs) {
        std::vector<spdlog::sink_ptr> sinks;
        for (auto &sink_name : cfg.sinks) {
            sinks.push_back(sink_map.at(sink_name));
        }
        auto &name = cfg.name;
        auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(cfg.level);

        logger_map[name] = logger;
    }

    for (auto &it : logger_map) {
        auto &name = it.first;
        auto &logger = it.second;
        if (name == "default") {
            if (overwrite_default) {
                auto prev = spdlog::get("default");
                logger->set_level(prev->level());
            }
            spdlog::set_default_logger(logger);
        } else {
            spdlog::register_logger(logger);
        }
    }

    return outcome::success();
}

} // namespace syncspirit::utils
