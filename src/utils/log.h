// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <memory>
#include <string_view>
#include <spdlog/spdlog.h>
#include "syncspirit-export.h"

namespace syncspirit::utils {

using logger_t = std::shared_ptr<spdlog::logger>;

SYNCSPIRIT_API logger_t get_logger(std::string_view name) noexcept;
SYNCSPIRIT_API spdlog::level::level_enum get_log_level(const std::string &log_level) noexcept;
SYNCSPIRIT_API std::string_view get_level_string(spdlog::level::level_enum) noexcept;

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
