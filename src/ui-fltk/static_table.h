#pragma once

#include <FL/Fl_Table.H>

#include <string>
#include <vector>
#include <utility>

namespace syncspirit::fltk {

struct table_row_t {
    std::string label;
    std::string value;
};

using table_rows_t = std::vector<table_row_t>;

struct static_table_t : Fl_Table {
    using parent_t = Fl_Table;

    static_table_t(table_rows_t &&rows, int x, int y, int w, int h);

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

  private:
    void draw_data(int row, int col, int x, int y, int w, int h);
    std::pair<int, int> calc_col_widths();

    table_rows_t table_rows;
};

} // namespace syncspirit::fltk
