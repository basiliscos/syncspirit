#pragma once

#include <string>
#include <deque>
#include <ostream>

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <spdlog/spdlog.h>

namespace syncspirit::fltk {

struct log_record_t : boost::intrusive_ref_counter<log_record_t, boost::thread_unsafe_counter> {
    log_record_t(spdlog::level::level_enum level_, std::string date_, std::string source_, std::string message_,
                 std::string thread_id_)
        : level{level_}, date{std::move(date_)}, source{std::move(source_)}, message{std::move(message_)},
          thread_id{std::move(thread_id_)} {}

    spdlog::level::level_enum level;
    std::string date;
    std::string source;
    std::string message;
    std::string thread_id;
};

using log_record_ptr_t = boost::intrusive_ptr<log_record_t>;
using log_records_t = std::deque<log_record_ptr_t>;

extern const char *eol;

void write(std::ostream &out, const log_record_t &record);

} // namespace syncspirit::fltk
