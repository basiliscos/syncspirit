#include "log_panel.h"

#include "log_table.h"
#include "log_colors.h"

#include <spdlog/fmt/fmt.h>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>

using namespace syncspirit::fltk;

struct syncspirit::fltk::fltk_sink_t final : base_sink_t {
    fltk_sink_t(log_panel_t *widget_) : widget{widget_} {}

    void forward(log_record_ptr_t record) override {
        auto lock = std::unique_lock(widget->incoming_mutex);
        widget->incoming_records.push_back(std::move(record));
        bool size = widget->incoming_records.size();
        lock.unlock();

        if (size == 1) {
            Fl::awake(
                [](void *data) {
                    auto widget = reinterpret_cast<log_panel_t *>(data);
                    auto lock = std::unique_lock(widget->incoming_mutex);
                    auto &source = widget->incoming_records;
                    auto &dest = widget->records;
                    std::move(begin(source), end(source), std::back_insert_iterator(dest));
                    source.clear();
                    widget->update();
                    lock.unlock();
                },
                widget);
        }
    }

    log_panel_t *widget;
};

static void auto_scroll_toggle(Fl_Widget *widget, void *data) {
    auto button = static_cast<Fl_Toggle_Button *>(widget);
    auto log_table = reinterpret_cast<log_table_t *>(data);
    log_table->autoscroll(button->value());
}

static void set_min_display_level(Fl_Widget *widget, void *data) {
    auto log_panel = static_cast<log_panel_t *>(widget->parent()->parent());
    auto level_ptr = reinterpret_cast<intptr_t>(data);
    auto level = static_cast<spdlog::level::level_enum>(level_ptr);
    log_panel->min_display_level(level);

    auto &level_buttons = log_panel->level_buttons;
    for (auto it = level_buttons.begin(); it != level_buttons.end(); ++it) {
        if (*it != widget) {
            (**it).value(0);
        }
    }
}

static void on_input_filter(Fl_Widget *widget, void *data) {
    auto input = reinterpret_cast<Fl_Input *>(widget);
    auto log_panel = reinterpret_cast<log_panel_t *>(data);
    log_panel->on_filter(input->value());
}

struct counter_label_t : Fl_Box {
    using parent_t = Fl_Box;
    using parent_t::parent_t;

    void draw() override {
        draw_box(box(), x(), y(), w(), h(), FL_BACKGROUND_COLOR);
        fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_RIGHT);
    }
};

log_panel_t::log_panel_t(application_t &application_, int x, int y, int w, int h)
    : parent_t{x, y, w, h}, application{application_}, display_level{spdlog::level::trace}

{
    int padding = 5;
    bool auto_scroll = true;

    auto bottom_row = 30;
    auto log_table_h = h - (padding * 2 + bottom_row);
    auto log_table = new log_table_t(displayed_records, padding, padding, w - padding * 2, log_table_h);
    log_table->autoscroll(auto_scroll);
    this->log_table = log_table;

    auto button_y = log_table->h() + padding * 2;
    auto button_h = bottom_row - padding;
    auto group = new Fl_Group(0, button_y, log_table->w(), button_h);

    auto autoscroll_button = new Fl_Toggle_Button(padding, button_y, 100, button_h, "auto scroll");
    autoscroll_button->value(auto_scroll);
    autoscroll_button->callback(auto_scroll_toggle, log_table);

    auto button_x = autoscroll_button->x() + autoscroll_button->w() + padding;
    auto trace_button = new Fl_Toggle_Button(button_x, button_y, button_h, button_h, " ");
    trace_button->color(log_colors[static_cast<int>(spdlog::level::trace) * 2]);
    trace_button->selection_color(log_colors[static_cast<int>(spdlog::level::trace) * 2 + 1]);
    trace_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::trace));
    trace_button->tooltip("display log records with trace level and above");
    level_buttons[0] = trace_button;
    button_x += trace_button->w() + 1;

    auto debug_button = new Fl_Toggle_Button(button_x, button_y, button_h, button_h, " ");
    debug_button->color(log_colors[static_cast<int>(spdlog::level::debug) * 2]);
    debug_button->selection_color(log_colors[static_cast<int>(spdlog::level::debug) * 2 + 1]);
    debug_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::debug));
    debug_button->tooltip("display log records with debug level and above");
    level_buttons[1] = debug_button;
    button_x += debug_button->w() + 1;

    auto info_button = new Fl_Toggle_Button(button_x, button_y, button_h, button_h, " ");
    info_button->color(log_colors[static_cast<int>(spdlog::level::info) * 2]);
    info_button->selection_color(log_colors[static_cast<int>(spdlog::level::info) * 2 + 1]);
    info_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::info));
    info_button->tooltip("display log records with info level and above");
    level_buttons[2] = info_button;
    button_x += info_button->w() + 1;

    auto warn_button = new Fl_Toggle_Button(button_x, button_y, button_h, button_h, " ");
    warn_button->color(log_colors[static_cast<int>(spdlog::level::warn) * 2]);
    warn_button->selection_color(log_colors[static_cast<int>(spdlog::level::warn) * 2 + 1]);
    warn_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::warn));
    warn_button->tooltip("display log records with warning level and above");
    level_buttons[3] = warn_button;
    button_x += warn_button->w() + 1;

    auto error_button = new Fl_Toggle_Button(button_x, button_y, button_h, button_h, " ");
    error_button->color(log_colors[static_cast<int>(spdlog::level::err) * 2]);
    error_button->selection_color(log_colors[static_cast<int>(spdlog::level::err) * 2 + 1]);
    error_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::err));
    error_button->tooltip("display log records with error level and above");
    level_buttons[4] = error_button;
    button_x += error_button->w() + 1;

    auto critical_button = new Fl_Toggle_Button(button_x, button_y, button_h, button_h, " ");
    critical_button->color(log_colors[static_cast<int>(spdlog::level::critical) * 2]);
    critical_button->selection_color(log_colors[static_cast<int>(spdlog::level::critical) * 2 + 1]);
    critical_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::critical));
    critical_button->tooltip("display log records with critical level");
    level_buttons[5] = critical_button;
    button_x += critical_button->w() + padding;

    auto filter_input = new Fl_Input(button_x, button_y, 200, button_h, "");
    filter_input->callback(on_input_filter, this);
    filter_input->when(FL_WHEN_CHANGED);
    button_x += filter_input->w() + padding;

    auto counter = new counter_label_t(padding + group->w() - 200, button_y, 200, button_h);
    records_counter = counter;

    group->end();
    group->resizable(counter);

    end();

    resizable(log_table);

    bridge_sink = sink_ptr_t(new fltk_sink_t(this));

    auto &dist_sink = application.dist_sink;
    for (auto &sink : dist_sink->sinks()) {
        auto in_memory_sink = dynamic_cast<im_memory_sink_t *>(sink.get());
        if (in_memory_sink) {
            std::lock_guard lock(in_memory_sink->mutex);
            records = std::move(in_memory_sink->records);
            dist_sink->remove_sink(sink);
            break;
        }
    }

    dist_sink->add_sink(bridge_sink);
}

log_panel_t::~log_panel_t() {
    application.dist_sink->remove_sink(bridge_sink);
    bridge_sink.reset();
}

void log_panel_t::update() {
    displayed_records.clear();
    for (auto &r : records) {
        bool display_record = (r->level >= display_level);
        if (display_record && !filter.empty()) {
            display_record =
                (r->source.find(filter) != std::string::npos) || (r->message.find(filter) != std::string::npos);
        }
        if (display_record) {
            displayed_records.push_back(r.get());
        }
    }
    log_table->update();

    char buff[64] = {0};
    auto message = fmt::format_to(buff, "{}/{}", displayed_records.size(), records.size());
    records_counter->copy_label(buff);
}

void log_panel_t::min_display_level(spdlog::level::level_enum level) {
    display_level = level;
    update();
}

void log_panel_t::on_filter(std::string_view filter_) {
    filter = filter_;
    update();
}
