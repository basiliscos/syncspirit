// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "log.h"
#include "config/log.h"
#include <boost/outcome.hpp>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "syncspirit-export.h"

namespace syncspirit::utils {

namespace outcome = boost::outcome_v2;

using file_sink_t = spdlog::sinks::basic_file_sink_mt;
using dist_sink_t = std::shared_ptr<spdlog::sinks::dist_sink_mt>;
using sink_t = spdlog::sink_ptr;

struct SYNCSPIRIT_API boostrap_guard_t {
    boostrap_guard_t(dist_sink_t dist_sink, file_sink_t *);
    ~boostrap_guard_t();
    dist_sink_t get_dist_sink();
    void discard();

  private:
    dist_sink_t dist_sink;
    file_sink_t *sink;
    bool discarded;
};
using boostrap_guard_ptr_t = std::unique_ptr<boostrap_guard_t>;

SYNCSPIRIT_API outcome::result<void> init_loggers(const config::log_configs_t &configs) noexcept;

SYNCSPIRIT_API dist_sink_t create_root_logger() noexcept;
SYNCSPIRIT_API boostrap_guard_ptr_t bootstrap(const spdlog::sink_ptr sink = {}) noexcept;

} // namespace syncspirit::utils
