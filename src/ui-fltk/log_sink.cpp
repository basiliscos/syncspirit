// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "log_sink.h"

using namespace syncspirit::fltk;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SECOND_FRACTION "%f"
#else
#define SECOND_FRACTION "%F"
#endif

in_memory_sink_t::in_memory_sink_t() : date_formatter("%H:%M:%S." SECOND_FRACTION) {}

void in_memory_sink_t::flush() {}

void in_memory_sink_t::set_pattern(const std::string &) {}

void in_memory_sink_t::set_formatter(std::unique_ptr<spdlog::formatter>) {}

void in_memory_sink_t::log(const spdlog::details::log_msg &msg) {
    spdlog::memory_buf_t formatted;
    date_formatter.format(msg, formatted);
    auto eob = formatted.end() - 1;
    while (!std::isdigit(*eob) && eob > formatted.begin()) {
        --eob;
    }
    ++eob;
    auto date = std::string(formatted.begin(), eob);
    auto source = [&]() { return std::string(msg.logger_name.begin(), msg.logger_name.end()); }();
    auto message = msg.payload;
    auto message_view = std::string_view(message.begin(), message.end());
    auto thread_id = std::to_string(msg.thread_id);

    std::string::size_type pos = 0, prev = 0;
    int index = 0;
    std::lock_guard guard(mutex);
    while ((pos = message_view.find("\n", prev)) != std::string::npos) {
        auto sub_message = message_view.substr(prev, pos - prev);
        auto record = [&]() -> log_record_ptr_t {
            if (!index) {
                ++index;
                return new log_record_t(msg.level, std::move(date), std::move(source), std::string(sub_message),
                                        std::move(thread_id));
            } else {
                return new log_record_t(msg.level, "", "", std::string(sub_message), "");
            }
        }();
        prev = pos + 1;
        records.emplace_back(std::move(record));
    }
    auto sub_message = message_view.substr(prev);
    auto record =
        new log_record_t(msg.level, std::move(date), std::move(source), std::string(sub_message), std::move(thread_id));
    records.emplace_back(std::move(record));
}

auto in_memory_sink_t::consume() -> log_queue_t {
    auto logs = log_queue_t();
    std::lock_guard guard(mutex);
    logs = std::move(records);
    return logs;
}
