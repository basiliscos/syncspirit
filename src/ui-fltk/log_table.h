#pragma once

#include <FL/Fl_Table_Row.H>
#include <deque>

#include "log_sink.h"

namespace syncspirit::fltk {

struct fltk_sink_t;

struct log_table_t : Fl_Table_Row {
    using parent_t = Fl_Table_Row;
    using displayed_records_t = std::deque<log_record_t *>;

    log_table_t(displayed_records_t &displayed_records, int x, int y, int w, int h);

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;
    void autoscroll(bool value);
    void update();
    int handle(int event) override;

  private:
    std::string gather_selected();
    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);

    displayed_records_t &displayed_records;
    bool auto_scrolling;
};

} // namespace syncspirit::fltk
