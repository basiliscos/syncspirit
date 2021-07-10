#pragma once
#include <memory>
#include <string>
#include <mutex>
#include <boost/outcome.hpp>
#include <spdlog/spdlog.h>
#include "../config/log.h"

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

using logger_t = std::shared_ptr<spdlog::logger>;

spdlog::level::level_enum get_log_level(const std::string &log_level) noexcept;

void set_default(const std::string &level, std::string &prompt, std::mutex &mutex) noexcept;

outcome::result<void> init_loggers(const config::log_configs_t &configs, std::string &prompt, std::mutex &mutex,
                                   bool overwrite_default) noexcept;

} // namespace syncspirit::utils
