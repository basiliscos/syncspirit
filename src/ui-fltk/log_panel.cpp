#include "log_panel.h"

#include "log_table.h"
#include "log_colors.h"

using namespace syncspirit::fltk;

static void auto_scroll_toggle(Fl_Widget *widget, void *data) {
    auto button = static_cast<Fl_Toggle_Button *>(widget);
    auto log_table = reinterpret_cast<log_table_t *>(data);
    log_table->autoscroll(button->value());
}

static void set_min_display_level(Fl_Widget *widget, void *data) {
    auto log_panel = static_cast<log_panel_t *>(widget->parent());
    auto log_table = static_cast<log_table_t *>(log_panel->log_table);
    auto level_ptr = reinterpret_cast<intptr_t>(data);
    auto level = static_cast<spdlog::level::level_enum>(level_ptr);
    log_table->min_display_level(level);

    auto &level_buttons = log_panel->level_buttons;
    for (auto it = level_buttons.begin(); it != level_buttons.end(); ++it) {
        if (*it != widget) {
            (**it).value(0);
        }
    }
}

log_panel_t::log_panel_t(application_t &application, int x, int y, int w, int h) : parent_t{x, y, w, h} {
    int padding = 5;
    bool auto_scroll = true;

    auto bottom_row = 30;
    auto log_table_h = h - (padding * 2 + bottom_row);
    auto log_table = new log_table_t(application, padding, padding, w - padding * 2, log_table_h);
    log_table->autoscroll(auto_scroll);
    this->log_table = log_table;

    auto button_y = log_table->h() + padding * 2;
    auto button_h = bottom_row - padding;
    auto autoscroll_button = new Fl_Toggle_Button(padding, button_y, 40, button_h, "auto-scroll");
    autoscroll_button->value(auto_scroll);
    autoscroll_button->callback(auto_scroll_toggle, log_table);

    auto button_x = autoscroll_button->x() + autoscroll_button->w() + padding;
    auto trace_button = new Fl_Toggle_Button(button_x, button_y, 10, button_h, " ");
    trace_button->color(log_colors[static_cast<int>(spdlog::level::trace) * 2]);
    trace_button->selection_color(log_colors[static_cast<int>(spdlog::level::trace) * 2 + 1]);
    trace_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::trace));
    trace_button->tooltip("display log records with trace level and above");
    level_buttons[0] = trace_button;
    button_x += trace_button->w() + 1;

    auto debug_button = new Fl_Toggle_Button(button_x, button_y, 10, button_h, " ");
    debug_button->color(log_colors[static_cast<int>(spdlog::level::debug) * 2]);
    debug_button->selection_color(log_colors[static_cast<int>(spdlog::level::debug) * 2 + 1]);
    debug_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::debug));
    debug_button->tooltip("display log records with debug level and above");
    level_buttons[1] = debug_button;
    button_x += debug_button->w() + 1;

    auto info_button = new Fl_Toggle_Button(button_x, button_y, 10, button_h, " ");
    info_button->color(log_colors[static_cast<int>(spdlog::level::info) * 2]);
    info_button->selection_color(log_colors[static_cast<int>(spdlog::level::info) * 2 + 1]);
    info_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::info));
    info_button->tooltip("display log records with info level and above");
    level_buttons[2] = info_button;
    button_x += info_button->w() + 1;

    auto warn_button = new Fl_Toggle_Button(button_x, button_y, 10, button_h, " ");
    warn_button->color(log_colors[static_cast<int>(spdlog::level::warn) * 2]);
    warn_button->selection_color(log_colors[static_cast<int>(spdlog::level::warn) * 2 + 1]);
    warn_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::warn));
    warn_button->tooltip("display log records with warning level and above");
    level_buttons[3] = warn_button;
    button_x += info_button->w() + 1;

    auto error_button = new Fl_Toggle_Button(button_x, button_y, 10, button_h, " ");
    error_button->color(log_colors[static_cast<int>(spdlog::level::err) * 2]);
    error_button->selection_color(log_colors[static_cast<int>(spdlog::level::err) * 2 + 1]);
    error_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::err));
    error_button->tooltip("display log records with error level and above");
    level_buttons[4] = error_button;
    button_x += info_button->w() + 1;

    auto critical_button = new Fl_Toggle_Button(button_x, button_y, 10, button_h, " ");
    critical_button->color(log_colors[static_cast<int>(spdlog::level::critical) * 2]);
    critical_button->selection_color(log_colors[static_cast<int>(spdlog::level::critical) * 2 + 1]);
    critical_button->callback(set_min_display_level, (void *)std::intptr_t(spdlog::level::critical));
    critical_button->tooltip("display log records with critical level");
    level_buttons[5] = critical_button;
    button_x += info_button->w() + 1;

    end();

    resizable(log_table);
}
