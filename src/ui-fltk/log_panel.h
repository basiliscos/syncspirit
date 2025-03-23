// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "app_supervisor.h"
#include "log_sink.h"
#include "log_table.h"

#include <FL/Fl_Group.H>
#include <FL/Fl_Toggle_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>
#include <array>

namespace syncspirit::fltk {

struct log_panel_t : Fl_Group {
    using parent_t = Fl_Group;
    using sink_ptr_t = spdlog::sink_ptr;
    using level_buttons_t = std::array<Fl_Toggle_Button *, 6>;

    log_panel_t(app_supervisor_t &supervisor, int x, int y, int w, int h);
    ~log_panel_t();

    void min_display_level(spdlog::level::level_enum level);
    void set_display_level(spdlog::level::level_enum level);
    void update();
    void update(log_queue_t new_records);
    void update_counter();
    void pull_in_logs();
    void on_filter(std::string_view filter);
    void set_splash_text(std::string text);
    void on_loading_done();

    app_supervisor_t &supervisor;
    Fl_Group *control_group;
    log_table_t *log_table;
    Fl_Box *records_counter;
    level_buttons_t level_buttons;
    log_queue_t incoming_records;
    log_buffer_ptr_t records;
    log_buffer_ptr_t displayed_records;
    std::string filter;
};

} // namespace syncspirit::fltk
