#pragma once

#include <FL/Fl_Table_Row.H>

#include <string>
#include <vector>
#include <utility>

namespace syncspirit::fltk {

struct table_row_t {
    std::string label;
    std::string value;

    template <typename L, typename V>
    table_row_t(L &&label_, V &&value_) : label{std::forward<L>(label_)}, value{std::forward<V>(value_)} {}
};

using table_rows_t = std::vector<table_row_t>;

struct static_table_t : Fl_Table_Row {
    using parent_t = Fl_Table_Row;

    struct col_sizes_t {
        int w1;
        int w2;
        int w1_min;
        int w2_min;
        bool invalid;
    };

    static_table_t(table_rows_t &&rows, int x, int y, int w, int h);

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;
    int handle(int event) override;
    void resize(int x, int y, int w, int h) override;
    void update_value(std::size_t row, std::string value);

  private:
    void draw_data(int row, int col, int x, int y, int w, int h);
    std::string gather_selected();
    col_sizes_t calc_col_widths();

    table_rows_t table_rows;
};

} // namespace syncspirit::fltk
