#pragma once

#include "category.h"
#include <FL/Fl_Table.H>

namespace syncspirit::fltk::config {

struct table_t : Fl_Table {
    using parent_t = Fl_Table;

    table_t(categories_t categories, int x, int y, int w, int h);
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

  private:
    void draw_data(int row, int col, int x, int y, int w, int h);
    categories_t categories;
};

} // namespace syncspirit::fltk::config
