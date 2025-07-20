// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "utils/log.h"
#include "log_utils.h"
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/sink.h>

namespace syncspirit::fltk {

struct in_memory_sink_t final : spdlog::sinks::sink {
    using mutex_t = std::mutex;

    in_memory_sink_t();
    void log(const spdlog::details::log_msg &msg) override;

    void flush() override;
    void set_pattern(const std::string &) override;
    void set_formatter(std::unique_ptr<spdlog::formatter>) override;

    log_queue_t consume();

  private:
    spdlog::pattern_formatter date_formatter;
    mutex_t mutex;
    log_queue_t records;
};

} // namespace syncspirit::fltk
