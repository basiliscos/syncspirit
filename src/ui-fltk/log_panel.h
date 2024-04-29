#pragma once

#include "application.h"
#include "log_sink.h"
#include "log_table.h"

#include <FL/Fl_Group.H>
#include <FL/Fl_Toggle_Button.H>
#include <array>

namespace syncspirit::fltk {

struct log_panel_t : Fl_Group {
    using parent_t = Fl_Group;
    using sink_ptr_t = spdlog::sink_ptr;
    using level_buttons_t = std::array<Fl_Toggle_Button *, 6>;
    using mutex_t = std::mutex;

    log_panel_t(application_t &application, int x, int y, int w, int h);
    ~log_panel_t();

    void min_display_level(spdlog::level::level_enum level);
    void update();

    application_t &application;
    sink_ptr_t bridge_sink;
    log_table_t *log_table;
    level_buttons_t level_buttons;
    mutex_t incoming_mutex;
    log_records_t incoming_records;
    log_records_t records;
    log_table_t::displayed_records_t displayed_records;
    spdlog::level::level_enum display_level;
};

} // namespace syncspirit::fltk