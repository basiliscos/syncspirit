#include "log.h"
#include "sink.h"
#include "error_code.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
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

static sink_option_t make_sink(std::string_view name, std::string &prompt, std::mutex &mutex,
                               bool interactive) noexcept {
    if (name == "interactive") {
        if (interactive) {
            return std::make_shared<sink_t>(stdout, spdlog::color_mode::automatic, mutex, prompt);
        } else {
            return std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        }
    } else if (name == "stdout") {
        return std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    } else if (name == "stderr") {
        return std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    }
    return utils::make_error_code(error_code_t::unknown_sink);
}

void set_default(const std::string &level, std::string &prompt, std::mutex &mutex, bool interactive) noexcept {
    auto sink = make_sink("interactive", prompt, mutex, interactive);
    auto logger = std::make_shared<spdlog::logger>("", sink.value());
    logger->set_level(get_log_level(level));
    spdlog::set_default_logger(logger);
}

outcome::result<void> init_loggers(const config::log_configs_t &configs, std::string &prompt, std::mutex &mutex,
                                   bool overwrite_default, bool interactive) noexcept {
    using sink_map_t = std::unordered_map<std::string, spdlog::sink_ptr>;
    using logger_map_t = std::unordered_map<std::string, std::shared_ptr<spdlog::logger>>;

    // init sinks
    auto prev = spdlog::default_logger();
    using log_t = decltype(prev);
    using guard_t = std::unique_ptr<log_t, std::function<void(log_t *)>>;
    guard_t guard(&prev, [](log_t *logger) { spdlog::set_default_logger(*logger); });

    spdlog::drop_all();
    sink_map_t sink_map;
    for (auto &cfg : configs) {
        for (auto &sink : cfg.sinks) {
            auto sink_option = make_sink(sink, prompt, mutex, interactive);
            if (!sink_option) {
                return sink_option.error();
            }
            sink_map[sink] = sink_option.value();
        }
    }

    logger_map_t logger_map;

    // init default
    std::vector<spdlog::sink_ptr> default_sinks;
    for (auto &cfg : configs) {
        auto &name = cfg.name;
        if (name != "default") {
            continue;
        }
        for (auto &sink_name : cfg.sinks) {
            default_sinks.push_back(sink_map.at(sink_name));
        }
        auto logger = std::make_shared<spdlog::logger>("", default_sinks.begin(), default_sinks.end());
        logger->set_level(cfg.level);

        logger_map[name] = logger;
    }

    if (default_sinks.empty()) {
        return utils::make_error_code(error_code_t::misconfigured_default_logger);
    }

    // init others
    for (auto &cfg : configs) {
        auto &name = cfg.name;
        if (name == "default") {
            continue;
        }

        std::vector<spdlog::sink_ptr> sinks;
        for (auto &sink_name : cfg.sinks) {
            sinks.push_back(sink_map.at(sink_name));
        }
        if (sinks.empty()) {
            sinks = default_sinks;
        }

        auto log_name = (name == "default") ? "" : name;
        auto logger = std::make_shared<spdlog::logger>(log_name, sinks.begin(), sinks.end());
        logger->set_level(cfg.level);

        logger_map[name] = logger;
    }

    // register
    for (auto &it : logger_map) {
        auto &name = it.first;
        auto &logger = it.second;
        if (name == "default") {
            if (overwrite_default && prev) {
                logger->set_level(prev->level());
            }
            spdlog::set_default_logger(logger);
        } else {
            spdlog::register_logger(logger);
        }
    }
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%L/%t%$] %v");

    guard.release();
    return outcome::success();
}

logger_t get_logger(std::string_view initial_name) noexcept {
    std::string name(initial_name);
    do {
        auto l = spdlog::get(name);
        if (l) {
            return l;
        }
        auto p = name.find_last_of(".");
        if (p != name.npos && p) {
            name = name.substr(0, p);
        } else {
            return spdlog::default_logger();
        }
    } while (1);
}

} // namespace syncspirit::utils
