#include "sink.h"
#include "spdlog/pattern_formatter.h"

using namespace spdlog;
using namespace syncspirit::console;

using mutex_t = std::mutex;

sink_t::sink_t(FILE *target_file, spdlog::color_mode mode, std::mutex &mutex_, std::string &prompt_) noexcept
    : mutex{mutex_}, prompt{prompt_}, target_file_(target_file),
      formatter_(details::make_unique<spdlog::pattern_formatter>()) {
    set_color_mode(mode);
    colors_[level::trace] = to_string_(white);
    colors_[level::debug] = to_string_(cyan);
    colors_[level::info] = to_string_(green);
    colors_[level::warn] = to_string_(yellow_bold);
    colors_[level::err] = to_string_(red_bold);
    colors_[level::critical] = to_string_(bold_on_red);
    colors_[level::off] = to_string_(reset);
}

sink_t::~sink_t() {
    print_ccode_(clear_promt);
    print_("\r", 1);
}

void sink_t::set_color(level::level_enum color_level, string_view_t color) {
    std::lock_guard<mutex_t> lock(mutex);
    colors_[color_level] = to_string_(color);
}

void sink_t::log(const spdlog::details::log_msg &msg) {

    // Wrap the originally formatted message in color codes.
    // If color is not supported in the terminal, log as is instead.
    std::lock_guard<mutex_t> lock(mutex);
    msg.color_range_start = 0;
    msg.color_range_end = 0;
    memory_buf_t formatted;
    formatter_->format(msg, formatted);
    if (should_do_colors_ && msg.color_range_end > msg.color_range_start) {
        print_ccode_(clear_promt);
        print_("\r", 1);
        // before color range
        print_range_(formatted, 0, msg.color_range_start);
        // in color range
        print_ccode_(colors_[msg.level]);
        print_range_(formatted, msg.color_range_start, msg.color_range_end);
        print_ccode_(reset);
        // after color range
        print_range_(formatted, msg.color_range_end, formatted.size());
        print_(prompt.data(), prompt.size());
    } else // no color
    {
        print_range_(formatted, 0, formatted.size());
    }
    fflush(target_file_);
}

void sink_t::flush() {
    std::lock_guard<mutex_t> lock(mutex);
    fflush(target_file_);
}

void sink_t::set_pattern(const std::string &pattern) {
    std::lock_guard<mutex_t> lock(mutex);
    formatter_ = std::unique_ptr<spdlog::formatter>(new pattern_formatter(pattern));
}

void sink_t::set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) {
    std::lock_guard<mutex_t> lock(mutex);
    formatter_ = std::move(sink_formatter);
}

bool sink_t::should_color() { return should_do_colors_; }

void sink_t::set_color_mode(color_mode mode) {
    switch (mode) {
    case color_mode::always:
        should_do_colors_ = true;
        return;
    case color_mode::automatic:
        should_do_colors_ = details::os::in_terminal(target_file_) && details::os::is_color_terminal();
        return;
    case color_mode::never:
        should_do_colors_ = false;
        return;
    }
}

void sink_t::print_ccode_(const string_view_t &color_code) {
    fwrite(color_code.data(), sizeof(char), color_code.size(), target_file_);
}

void sink_t::print_range_(const memory_buf_t &formatted, size_t start, size_t end) {
    fwrite(formatted.data() + start, sizeof(char), end - start, target_file_);
}

void sink_t::print_(const char *buff, size_t length) { fwrite(buff, sizeof(char), length, target_file_); }

std::string sink_t::to_string_(const string_view_t &sv) { return std::string(sv.data(), sv.size()); }
