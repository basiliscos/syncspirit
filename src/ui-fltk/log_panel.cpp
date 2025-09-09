// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "log_panel.h"

#include "log_table.h"
#include "log_colors.h"
#include "utils/io.h"

#include <spdlog/fmt/fmt.h>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <boost/algorithm/string/replace.hpp>

using fmt::format_to;

using namespace syncspirit::fltk;

static constexpr int padding = 5;

static void _pull_in_logs(void *data) {
    reinterpret_cast<log_panel_t *>(data)->pull_in_logs();
    Fl::add_timeout(0.05, _pull_in_logs, data);
}

static void auto_scroll_toggle(Fl_Widget *widget, void *data) {
    auto button = static_cast<Fl_Toggle_Button *>(widget);
    auto log_table = reinterpret_cast<log_table_t *>(data);
    log_table->autoscroll(button->value());
}

static void clear_logs(Fl_Widget *, void *data) {
    auto panel = reinterpret_cast<log_panel_t *>(data);
    panel->displayed_records->clear();
    panel->records->clear();
    panel->log_table->update();
    panel->log_table->redraw();
}

static void set_min_display_level(Fl_Widget *widget, void *data) {
    auto log_panel = static_cast<log_panel_t *>(widget->parent()->parent());
    auto level_ptr = reinterpret_cast<intptr_t>(data);
    auto level = static_cast<spdlog::level::level_enum>(level_ptr);
    log_panel->min_display_level(level);

    auto &level_buttons = log_panel->level_buttons;
    for (auto it = level_buttons.begin(); it != level_buttons.end(); ++it) {
        (*it)->activate();
        if (*it != widget) {
            (**it).value(0);
        }
    }
    widget->deactivate();
}

static void on_input_filter(Fl_Widget *widget, void *data) {
    auto input = reinterpret_cast<Fl_Input *>(widget);
    auto log_panel = reinterpret_cast<log_panel_t *>(data);
    log_panel->on_filter(input->value());
}

static void export_log(Fl_Widget *, void *data) {
    struct it_t final : logs_iterator_t {
        it_t(log_buffer_t *buff_) : buff{buff_}, i{0} {}

        log_record_t *next() override {
            if (i < buff->size()) {
                return (*buff)[i++].get();
            }
            return {};
        }

        log_buffer_t *buff;
        size_t i;
    };

    Fl_Native_File_Chooser file_chooser;
    file_chooser.title("Save syncspirit log");
    file_chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    file_chooser.filter("CSV files\t*.csv");

    auto log_panel = reinterpret_cast<log_panel_t *>(data);
    auto it_selected = log_panel->log_table->get_selected();
    if (!it_selected) {
        it_selected.reset(new it_t(log_panel->displayed_records.get()));
    }

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
    using file_t = syncspirit::utils::fstream_t;
    auto out = file_t(filename, file_t::binary);
    out << "level, date, source, message" << eol;

    auto escape_message = [](const std::string &msg) -> std::string {
        auto copy = boost::replace_all_copy<std::string>(msg, "\"", "\"\"");
        return fmt::format("\"{}\"", copy);
    };

    while (auto record = it_selected->next()) {
        out << record->level << ", " << record->date << ", " << record->source << ", "
            << escape_message(record->message) << eol;
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
    : parent_t{x, y, w, h}, supervisor{supervisor_}, records_counter{nullptr} {
    auto &cfg = supervisor.get_app_config().fltk_config;
    records = std::make_unique<log_buffer_t>(cfg.log_records_buffer);
    displayed_records = std::make_unique<log_buffer_t>(cfg.log_records_buffer);

    auto bottom_row = 30;
    auto log_table_h = h - (padding * 2 + bottom_row);
    auto log_table = new log_table_t(displayed_records, padding, padding, w - padding * 2, log_table_h);
    log_table->autoscroll(true);
    this->log_table = log_table;

    auto common_y = log_table->h() + padding * 2;
    auto common_h = bottom_row - padding;

    bool auto_scroll = true;
    control_group = new Fl_Group(0, common_y, log_table->w(), common_h);
    control_group->begin();
    auto autoscroll_button = new Fl_Toggle_Button(padding, common_y, 100, common_h, "auto scroll");
    autoscroll_button->value(auto_scroll);
    autoscroll_button->callback(auto_scroll_toggle, log_table);

    auto button_x = autoscroll_button->x() + autoscroll_button->w() + padding;

    auto clear_button = new Fl_Button(button_x, common_y, 80, common_h, "clear");
    clear_button->callback(clear_logs, this);
    button_x += clear_button->w() + padding;

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

    auto counter = new counter_label_t(padding + control_group->w() - 200, common_y, 200, common_h);
    counter->tooltip("displayed number of records / total number of records");
    records_counter = counter;

    control_group->end();
    control_group->resizable(counter);

    control_group->end();

    end();

    resizable(log_table);

    sink = supervisor.get_log_sink();

    update();
}

void log_panel_t::on_loading_done() {
    pull_in_logs();
    Fl::add_timeout(0.05, _pull_in_logs, this);
}

log_panel_t::~log_panel_t() { Fl::remove_timeout(_pull_in_logs, this); }

void log_panel_t::update(log_queue_t new_records) {
    auto display_level = supervisor.get_app_config().fltk_config.level;
    for (auto &r : new_records) {
        bool display_record = (r->level >= display_level);
        if (display_record && !filter.empty()) {
            display_record =
                (r->source.find(filter) != std::string::npos) || (r->message.find(filter) != std::string::npos);
        }
        if (display_record) {
            displayed_records->push_back(r.get());
        }
        records->push_back(r);
    }
    log_table->update();
    update_counter();
}

void log_panel_t::update() {
    displayed_records->clear();

    auto display_level = supervisor.get_app_config().fltk_config.level;
    for (auto &r : *records) {
        bool display_record = (r->level >= display_level);
        if (display_record && !filter.empty()) {
            display_record =
                (r->source.find(filter) != std::string::npos) || (r->message.find(filter) != std::string::npos);
        }
        if (display_record) {
            displayed_records->push_back(r.get());
        }
    }
    log_table->update();
    update_counter();
}

void log_panel_t::update_counter() {
    if (records_counter) {
        char buff[64] = {0};
        format_to(buff, "{}/{}", displayed_records->size(), records->size());
        records_counter->copy_label(buff);
    }
}

void log_panel_t::min_display_level(spdlog::level::level_enum level) {
    supervisor.get_app_config().fltk_config.level = level;
    update();
}

void log_panel_t::on_filter(std::string_view filter_) {
    filter = filter_;
    update();
}

void log_panel_t::set_splash_text(std::string text) {
    supervisor.get_logger()->info(text);
    pull_in_logs();
}

void log_panel_t::pull_in_logs() {
    auto logs = sink->consume();
    if (logs.size()) {
        update(std::move(logs));
    }
}
