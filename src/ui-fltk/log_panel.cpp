#include "log_panel.h"

#include "log_table.h"
#include "log_colors.h"

#include <spdlog/fmt/fmt.h>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <boost/algorithm/string/replace.hpp>

#include <fstream>

using namespace syncspirit::fltk;

struct syncspirit::fltk::fltk_sink_t final : base_sink_t {
    fltk_sink_t(log_panel_t *widget_) : widget{widget_} {}

    void forward(log_record_ptr_t record) override {
        auto lock = std::unique_lock(widget->incoming_mutex);
        widget->incoming_records.push_back(std::move(record));
    }

    log_panel_t *widget;
};

static void pull_in_logs(void *data) {
    auto widget = reinterpret_cast<log_panel_t *>(data);
    auto lock = std::unique_lock(widget->incoming_mutex);
    auto &source = widget->incoming_records;
    if (source.size()) {
        auto &dest = widget->records;
        std::move(begin(source), end(source), std::back_insert_iterator(dest));
        source.clear();
        lock.unlock();
        widget->update();
    }
    Fl::add_timeout(0.05, pull_in_logs, data);
}

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

static void export_log(Fl_Widget *widget, void *data) {
    auto input = reinterpret_cast<Fl_Input *>(widget);
    auto log_panel = reinterpret_cast<log_panel_t *>(data);

    Fl_Native_File_Chooser file_chooser;
    file_chooser.title("Save synspirit log");
    file_chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    file_chooser.filter("CSV files\t*.csv");

    auto selected_records = log_panel->log_table->get_selected();
    auto log_source = selected_records.empty() ? &log_panel->records : &selected_records;

    auto r = file_chooser.show();
    auto &log = log_panel->supervisor.get_logger();
    if (r == -1) {
        log->error("cannot chose file: {}", file_chooser.errmsg());
        return;
    } else if (r == 1) { // cancel
        return;
    }

    // write
    auto filename = file_chooser.filename();
    auto out = std::ofstream(filename, std::ofstream::binary);
    out << "level, date, source, message" << eol;

    auto escape_message = [](const std::string &msg) -> std::string {
        auto copy = boost::replace_all_copy<std::string>(msg, "\"", "\"\"");
        return fmt::format("\"{}\"", copy);
    };

    for (size_t i = 0; i < log_source->size(); ++i) {
        auto &record = *log_source->at(i);
        out << record.level << ", " << record.date << ", " << record.source << ", " << escape_message(record.message)
            << eol;
    }
    log->info("logs wrote to {}", filename);
}

struct counter_label_t : Fl_Box {
    using parent_t = Fl_Box;
    using parent_t::parent_t;

    void draw() override {
        draw_box(box(), x(), y(), w(), h(), FL_BACKGROUND_COLOR);
        fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_RIGHT);
    }
};

log_panel_t::log_panel_t(app_supervisor_t &supervisor_, int x, int y, int w, int h)
    : parent_t{x, y, w, h}, supervisor{supervisor_} {
    int padding = 5;
    bool auto_scroll = true;

    auto bottom_row = 30;
    auto log_table_h = h - (padding * 2 + bottom_row);
    auto log_table = new log_table_t(displayed_records, padding, padding, w - padding * 2, log_table_h);
    log_table->autoscroll(auto_scroll);
    this->log_table = log_table;

    auto common_y = log_table->h() + padding * 2;
    auto common_h = bottom_row - padding;
    auto group = new Fl_Group(0, common_y, log_table->w(), common_h);

    auto autoscroll_button = new Fl_Toggle_Button(padding, common_y, 100, common_h, "auto scroll");
    autoscroll_button->value(auto_scroll);
    autoscroll_button->callback(auto_scroll_toggle, log_table);

    auto button_x = autoscroll_button->x() + autoscroll_button->w() + padding;

    auto export_button = new Fl_Button(button_x, common_y, 80, common_h, "export...");
    export_button->callback(export_log, this);
    button_x += export_button->w() + padding;

    auto trace_button = new Fl_Toggle_Button(button_x, common_y, common_h, common_h, " ");
    trace_button->color(log_colors[static_cast<int>(spdlog::level::trace) * 2]);
    trace_button->selection_color(log_colors[static_cast<int>(spdlog::level::trace) * 2 + 1]);
    trace_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::trace));
    trace_button->tooltip("display log records with trace level and above");
    level_buttons[0] = trace_button;
    button_x += trace_button->w() + 1;

    auto debug_button = new Fl_Toggle_Button(button_x, common_y, common_h, common_h, " ");
    debug_button->color(log_colors[static_cast<int>(spdlog::level::debug) * 2]);
    debug_button->selection_color(log_colors[static_cast<int>(spdlog::level::debug) * 2 + 1]);
    debug_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::debug));
    debug_button->tooltip("display log records with debug level and above");
    level_buttons[1] = debug_button;
    button_x += debug_button->w() + 1;

    auto info_button = new Fl_Toggle_Button(button_x, common_y, common_h, common_h, " ");
    info_button->color(log_colors[static_cast<int>(spdlog::level::info) * 2]);
    info_button->selection_color(log_colors[static_cast<int>(spdlog::level::info) * 2 + 1]);
    info_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::info));
    info_button->tooltip("display log records with info level and above");
    level_buttons[2] = info_button;
    button_x += info_button->w() + 1;

    auto warn_button = new Fl_Toggle_Button(button_x, common_y, common_h, common_h, " ");
    warn_button->color(log_colors[static_cast<int>(spdlog::level::warn) * 2]);
    warn_button->selection_color(log_colors[static_cast<int>(spdlog::level::warn) * 2 + 1]);
    warn_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::warn));
    warn_button->tooltip("display log records with warning level and above");
    level_buttons[3] = warn_button;
    button_x += warn_button->w() + 1;

    auto error_button = new Fl_Toggle_Button(button_x, common_y, common_h, common_h, " ");
    error_button->color(log_colors[static_cast<int>(spdlog::level::err) * 2]);
    error_button->selection_color(log_colors[static_cast<int>(spdlog::level::err) * 2 + 1]);
    error_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::err));
    error_button->tooltip("display log records with error level and above");
    level_buttons[4] = error_button;
    button_x += error_button->w() + 1;

    auto critical_button = new Fl_Toggle_Button(button_x, common_y, common_h, common_h, " ");
    critical_button->color(log_colors[static_cast<int>(spdlog::level::critical) * 2]);
    critical_button->selection_color(log_colors[static_cast<int>(spdlog::level::critical) * 2 + 1]);
    critical_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::critical));
    critical_button->tooltip("display log records with critical level");
    level_buttons[5] = critical_button;
    button_x += critical_button->w() + padding;

    auto enable_button = static_cast<std::size_t>(supervisor.get_app_config().fltk_config.level);
    if (enable_button < level_buttons.size()) {
        level_buttons[enable_button]->value(1);
    }

    auto filter_input = new Fl_Input(button_x, common_y, 200, common_h, "");
    filter_input->callback(on_input_filter, this);
    filter_input->when(FL_WHEN_CHANGED);
    button_x += filter_input->w() + padding;

    auto counter = new counter_label_t(padding + group->w() - 200, common_y, 200, common_h);
    counter->tooltip("displayed number of records / total number of records");
    records_counter = counter;

    group->end();
    group->resizable(counter);

    end();

    resizable(log_table);

    bridge_sink = sink_ptr_t(new fltk_sink_t(this));

    auto &dist_sink = supervisor.get_dist_sink();
    for (auto sink : dist_sink->sinks()) {
        auto in_memory_sink = dynamic_cast<im_memory_sink_t *>(sink.get());
        if (in_memory_sink) {
            std::lock_guard lock(in_memory_sink->mutex);
            records = std::move(in_memory_sink->records);
            dist_sink->remove_sink(sink);
            break;
        }
    }

    Fl::add_timeout(0.05, pull_in_logs, this);

    dist_sink->add_sink(bridge_sink);
}

log_panel_t::~log_panel_t() {
    Fl::remove_timeout(pull_in_logs, this);
    supervisor.get_dist_sink()->remove_sink(bridge_sink);
    bridge_sink.reset();
}

void log_panel_t::update() {
    displayed_records.clear();
    auto display_level = supervisor.get_app_config().fltk_config.level;
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
    supervisor.get_app_config().fltk_config.level = level;
    update();
}

void log_panel_t::on_filter(std::string_view filter_) {
    filter = filter_;
    update();
}
