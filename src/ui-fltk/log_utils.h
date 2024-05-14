#pragma once

#include <string>
#include <memory>
#include <deque>
#include <ostream>

#include <spdlog/spdlog.h>

namespace syncspirit::fltk {

struct log_record_t {
    spdlog::level_t level;
    std::string date;
    std::string source;
    std::string message;
    std::string thread_id;
};

using log_record_ptr_t = std::unique_ptr<log_record_t>;
using log_records_t = std::deque<log_record_ptr_t>;

extern const char *eol;

void write(std::ostream &out, const log_record_t &record);

} // namespace syncspirit::fltk
