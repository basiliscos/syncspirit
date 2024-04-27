#pragma once

#include <FL/Fl_Table.H>
#include <vector>
#include <memory>

#include "log_sink.h"
#include "utils/log.h"

namespace syncspirit::fltk {

struct log_panel_t : Fl_Table {
    using parent_t = Fl_Table;

    using sink_ptr_t = spdlog::sink_ptr;

    log_panel_t(utils::dist_sink_t dist_sink, int x, int y, int w, int h);
    ~log_panel_t();

    void append(log_record_ptr_t record);

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

  private:
    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);

    log_records_t records;
    sink_ptr_t bridge_sink;
    utils::dist_sink_t dist_sink;
};

} // namespace syncspirit::fltk
