// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "log.h"

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

std::string_view get_level_string(spdlog::level::level_enum level) noexcept {
    using L = spdlog::level::level_enum;
    switch (level) {
    case L::trace:
        return "trace";
    case L::debug:
        return "debug";
    case L::info:
        return "info";
    case L::warn:
        return "info";
    case L::err:
        return "error";
    case L::critical:
        return "critical";
    default:
        return "off";
    }
}

logger_t get_logger(std::string_view initial_name) noexcept {
    std::string name(initial_name);
    logger_t result = spdlog::get(name);
    if (result) {
        return result;
    }

    logger_t parent;
    while (!parent) {
        auto p = name.find_last_of(".");
        if (p != name.npos) {
            name = name.substr(0, p);
            parent = spdlog::get(name);
        } else {
            parent = spdlog::default_logger();
        }
    }

    auto &sinks = parent->sinks();
    auto log_name = std::string(initial_name);
    result = std::make_shared<spdlog::logger>(log_name, sinks.begin(), sinks.end());
    result->set_level(parent->level());
    spdlog::register_logger(result);
    return result;
}

} // namespace syncspirit::utils
