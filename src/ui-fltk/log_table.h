#pragma once

#include "log_utils.h"
#include <FL/Fl_Table_Row.H>
#include <boost/circular_buffer.hpp>

namespace syncspirit::fltk {

struct fltk_sink_t;

struct log_table_t : Fl_Table_Row {
    using parent_t = Fl_Table_Row;

    log_table_t(log_buffer_ptr_t &displayed_records, int x, int y, int w, int h);

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;
    void autoscroll(bool value);
    void update();
    int handle(int event) override;
    log_iterator_ptr_t get_selected();

  private:
    using selected_indices_t = std::vector<std::size_t>;
    std::string gather_selected();
    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);

    selected_indices_t selected_records;
    log_buffer_ptr_t &displayed_records;
    bool auto_scrolling;
};

} // namespace syncspirit::fltk
