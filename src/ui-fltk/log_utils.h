// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include <string>
#include <vector>
#include <memory>

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/circular_buffer.hpp>
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

struct logs_iterator_t {
    virtual ~logs_iterator_t() = default;
    virtual log_record_t *next() = 0;
};

using log_record_ptr_t = boost::intrusive_ptr<log_record_t>;
using log_buffer_t = boost::circular_buffer<log_record_ptr_t>;
using log_buffer_ptr_t = std::unique_ptr<log_buffer_t>;
using log_queue_t = std::vector<log_record_ptr_t>;
using log_iterator_ptr_t = std::unique_ptr<logs_iterator_t>;

extern const char *eol;

} // namespace syncspirit::fltk
