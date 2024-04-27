#include "log_sink.h"

using namespace syncspirit::fltk;

base_sink_t::base_sink_t() : date_formatter("%Y-%m-%d %H:%M:%S.%F") {}

void base_sink_t::flush() {}

void base_sink_t::set_pattern(const std::string &) {}

void base_sink_t::set_formatter(std::unique_ptr<spdlog::formatter>) {}

void base_sink_t::log(const spdlog::details::log_msg &msg) {
    spdlog::memory_buf_t formatted;
    date_formatter.format(msg, formatted);
    auto date = std::string(formatted.begin(), formatted.end() - 1);
    auto source = msg.logger_name;
    auto message = msg.payload;

    auto record =
        std::make_unique<log_record_t>(msg.level, std::move(date), std::string(source.begin(), source.end()),
                                       std::string(message.begin(), message.end()), std::to_string(msg.thread_id));

    forward(std::move(record));
}
