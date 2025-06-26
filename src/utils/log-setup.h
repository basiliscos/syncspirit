// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "log.h"
#include "config/log.h"
#include <boost/outcome.hpp>
#include <spdlog/sinks/dist_sink.h>
#include <filesystem>
#include "syncspirit-export.h"

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;
namespace bfs = std::filesystem;

using dist_sink_t = std::shared_ptr<spdlog::sinks::dist_sink_mt>;
using sink_t = spdlog::sink_ptr;

struct SYNCSPIRIT_API bootstrap_guard_t {
    bootstrap_guard_t(dist_sink_t dist_sink, spdlog::sinks::sink *);
    ~bootstrap_guard_t();
    dist_sink_t get_dist_sink();

  private:
    dist_sink_t dist_sink;
    spdlog::sinks::sink *sink;
};
using bootstrap_guard_ptr_t = std::unique_ptr<bootstrap_guard_t>;

SYNCSPIRIT_API outcome::result<void> init_loggers(const config::log_configs_t &configs) noexcept;
SYNCSPIRIT_API void finalize_loggers() noexcept;

SYNCSPIRIT_API dist_sink_t create_root_logger() noexcept;
SYNCSPIRIT_API bootstrap_guard_ptr_t bootstrap(dist_sink_t &, const bfs::path &dir) noexcept;

} // namespace syncspirit::utils
