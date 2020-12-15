#pragma once
#include <spdlog/sinks/sink.h>
#include <mutex>

namespace syncspirit {
namespace console {

struct sink_t : spdlog::sinks::sink {
    sink_t(FILE *target_file, spdlog::color_mode mode, std::mutex &mutex_, std::string &prompt_) noexcept;
    ~sink_t();

    void set_color(spdlog::level::level_enum color_level, spdlog::string_view_t color);
    void set_color_mode(spdlog::color_mode mode);
    bool should_color();

    void log(const spdlog::details::log_msg &msg) override;
    void flush() override;
    void set_pattern(const std::string &pattern) override;
    void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override;

    // Formatting codes
    static const constexpr spdlog::string_view_t reset = "\033[m";
    static const constexpr spdlog::string_view_t bold = "\033[1m";
    static const constexpr spdlog::string_view_t dark = "\033[2m";
    static const constexpr spdlog::string_view_t underline = "\033[4m";
    static const constexpr spdlog::string_view_t blink = "\033[5m";
    static const constexpr spdlog::string_view_t reverse = "\033[7m";
    static const constexpr spdlog::string_view_t concealed = "\033[8m";
    static const constexpr spdlog::string_view_t clear_line = "\033[K";
    static const constexpr spdlog::string_view_t clear_promt = "\033[1K";

    // Foreground colors
    static const constexpr spdlog::string_view_t black = "\033[30m";
    static const constexpr spdlog::string_view_t red = "\033[31m";
    static const constexpr spdlog::string_view_t green = "\033[32m";
    static const constexpr spdlog::string_view_t yellow = "\033[33m";
    static const constexpr spdlog::string_view_t blue = "\033[34m";
    static const constexpr spdlog::string_view_t magenta = "\033[35m";
    static const constexpr spdlog::string_view_t cyan = "\033[36m";
    static const constexpr spdlog::string_view_t white = "\033[37m";

    /// Background colors
    const spdlog::string_view_t on_black = "\033[40m";
    const spdlog::string_view_t on_red = "\033[41m";
    const spdlog::string_view_t on_green = "\033[42m";
    const spdlog::string_view_t on_yellow = "\033[43m";
    const spdlog::string_view_t on_blue = "\033[44m";
    const spdlog::string_view_t on_magenta = "\033[45m";
    const spdlog::string_view_t on_cyan = "\033[46m";
    const spdlog::string_view_t on_white = "\033[47m";

    /// Bold colors
    const spdlog::string_view_t yellow_bold = "\033[33m\033[1m";
    const spdlog::string_view_t red_bold = "\033[31m\033[1m";
    const spdlog::string_view_t bold_on_red = "\033[1m\033[41m";

  private:
    std::mutex &mutex;
    std::string &prompt;
    FILE *target_file_;
    bool should_do_colors_;
    std::unique_ptr<spdlog::formatter> formatter_;
    std::array<std::string, spdlog::level::n_levels> colors_;
    void print_ccode_(const spdlog::string_view_t &color_code);
    void print_range_(const spdlog::memory_buf_t &formatted, size_t start, size_t end);
    void print_(const char *, size_t length);
    static std::string to_string_(const spdlog::string_view_t &sv);
};

} // namespace console
} // namespace syncspirit
