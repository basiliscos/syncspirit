#pragma once

#include "utils/log.h"

namespace syncspirit::fltk {

struct log_record_t {
    spdlog::level_t level;
    std::string date;
    std::string source;
    std::string message;
    std::string thread_id;
};

using log_record_ptr_t = std::unique_ptr<log_record_t>;

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

} // namespace syncspirit::fltk
