#include "log_panel.h"

#include <FL/Fl.H>
#include <spdlog/sinks/sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/fmt/fmt.h>
#include <atomic>
#include <array>

using namespace syncspirit::fltk;

using log_level_t = spdlog::level_t;

struct log_panel_t::log_record_t {
    log_level_t level;
    std::string date;
    std::string source;
    std::string message;
    size_t thread_id;
};

namespace {

std::atomic_bool destroyed{false};

struct fltk_sink_t final : spdlog::sinks::sink {
    fltk_sink_t(log_panel_t *widget_) : widget{widget_}, date_formatter("%Y-%m-%d %H:%M:%S.%F") {}

    void log(const spdlog::details::log_msg &msg) override {
        struct context_t {
            log_panel_t *widget;
            log_panel_t::log_record_ptr_t record;
        };

        spdlog::memory_buf_t formatted;
        date_formatter.format(msg, formatted);
        auto date = std::string(formatted.begin(), formatted.end() - 1);
        auto source = msg.logger_name;
        auto message = msg.payload;

        auto record = std::make_unique<log_panel_t::log_record_t>(
            msg.level, std::move(date), std::string(source.begin(), source.end()),
            std::string(message.begin(), message.end()), msg.thread_id);

        Fl::awake(
            [](void *data) {
                auto context = reinterpret_cast<context_t *>(data);
                auto record = std::move(context->record);
                if (!destroyed) {
                    context->widget->append(std::move(record));
                }
                delete context;
            },
            new context_t(widget, std::move(record)));
    }

    void flush() override {}
    void set_pattern(const std::string &) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

    log_panel_t *widget;
    spdlog::pattern_formatter date_formatter;
};

using style_entry_t = Fl_Text_Display::Style_Table_Entry;
template <int N> using array_of_styles_t = std::array<style_entry_t, N>;

array_of_styles_t<6> styles = {
    style_entry_t(FL_RED, FL_SCREEN, 18, 0),        style_entry_t(FL_DARK_YELLOW, FL_SCREEN, 18, 0),
    style_entry_t(FL_DARK_GREEN, FL_SCREEN, 18, 0), style_entry_t(FL_BLUE, FL_SCREEN, 18, 0),
    style_entry_t(FL_CYAN, FL_SCREEN, 18, 0),       style_entry_t(FL_GRAY0, FL_SCREEN, 18, 0),
};

} // namespace

log_panel_t::log_panel_t(utils::dist_sink_t dist_sink_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), dist_sink{dist_sink_} {

    buffer(&text_buffer);
    highlight_data(&style_buffer, styles.data(), styles.size(), 'A', 0, 0);

    bridge_sink = sink_ptr_t(new fltk_sink_t(this));
    dist_sink->add_sink(bridge_sink);
}

log_panel_t::~log_panel_t() {
    dist_sink->remove_sink(bridge_sink);
    buffer(nullptr);
    destroyed = true;
}

void log_panel_t::append(log_record_ptr_t record) {
    // auto string = fmt::format("[{}] {} {}\n",  record->thread_id, record->source, record->message);
    auto string = record->date + record->message + "\n";
    text_buffer.append(string.data());
    auto hilight_char = 'A' + (int)record->level;
    auto hilight_string = std::string(string.size(), hilight_char);
    style_buffer.append(hilight_string.data());

    records.emplace_back(std::move(record));
}
