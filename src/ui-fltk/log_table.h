#pragma once

#include <FL/Fl_Table.H>
#include <vector>
#include <memory>
#include <mutex>

#include "log_sink.h"
#include "application.h"
#include "utils/log.h"

namespace syncspirit::fltk {

struct fltk_sink_t;

struct log_panel_t : Fl_Table {
    using parent_t = Fl_Table;

    using sink_ptr_t = spdlog::sink_ptr;

    log_panel_t(application_t &application, int x, int y, int w, int h);
    ~log_panel_t();

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

  private:
    using mutex_t = std::mutex;
    void update();
    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);

    application_t &application;
    mutex_t incoming_mutex;
    log_records_t incoming_records;
    log_records_t records;
    sink_ptr_t bridge_sink;

    friend fltk_sink_t;
};

} // namespace syncspirit::fltk
