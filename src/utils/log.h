// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <boost/outcome.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/dist_sink.h>
#include "config/log.h"
#include "syncspirit-export.h"

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

using logger_t = std::shared_ptr<spdlog::logger>;
using dist_sink_t = std::shared_ptr<spdlog::sinks::dist_sink_mt>;

SYNCSPIRIT_API spdlog::level::level_enum get_log_level(const std::string &log_level) noexcept;
SYNCSPIRIT_API std::string_view get_level_string(spdlog::level::level_enum) noexcept;

SYNCSPIRIT_API void set_default(const std::string &level) noexcept;

SYNCSPIRIT_API outcome::result<dist_sink_t> init_loggers(const config::log_configs_t &configs,
                                                         bool overwrite_default) noexcept;

SYNCSPIRIT_API logger_t get_logger(std::string_view name) noexcept;

#define LOG_GENERIC(LOGGER, LEVEL, ...)                                                                                \
    if (LEVEL >= LOGGER->level())                                                                                      \
    LOGGER->log(LEVEL, __VA_ARGS__)

#define LOG_TRACE(LOGGER, ...) LOG_GENERIC(LOGGER, spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(LOGGER, ...) LOG_GENERIC(LOGGER, spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(LOGGER, ...) LOG_GENERIC(LOGGER, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(LOGGER, ...) LOG_GENERIC(LOGGER, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(LOGGER, ...) LOG_GENERIC(LOGGER, spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(LOGGER, ...) LOG_GENERIC(LOGGER, spdlog::level::critical, __VA_ARGS__)

} // namespace syncspirit::utils
