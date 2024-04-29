#pragma once

#include <FL/Fl_Table.H>
#include <vector>
#include <memory>
#include <mutex>

#include "log_sink.h"
#include "application.h"

namespace syncspirit::fltk {

struct fltk_sink_t;

struct log_table_t : Fl_Table {
    using parent_t = Fl_Table;

    using sink_ptr_t = spdlog::sink_ptr;

    log_table_t(application_t &application, int x, int y, int w, int h);
    ~log_table_t();

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;
    void autoscroll(bool value);

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
    bool auto_scrolling;

    friend fltk_sink_t;
};

} // namespace syncspirit::fltk
