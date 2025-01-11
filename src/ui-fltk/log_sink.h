#pragma once

#include "utils/log.h"
#include "log_utils.h"
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/sink.h>

namespace syncspirit::fltk {

struct base_sink_t : spdlog::sinks::sink {
    base_sink_t();

    void log(const spdlog::details::log_msg &msg) override;

    void flush() override;
    void set_pattern(const std::string &) override;
    void set_formatter(std::unique_ptr<spdlog::formatter>) override;
    virtual void forward(log_record_ptr_t) = 0;

  private:
    spdlog::pattern_formatter date_formatter;
};

struct im_memory_sink_t final : base_sink_t {
    using base_sink_t::base_sink_t;
    using mutex_t = std::mutex;

    void forward(log_record_ptr_t) override;

    mutex_t mutex;
    log_queue_t records;
};

} // namespace syncspirit::fltk
