#pragma once

#include "category.h"
#include <FL/Fl_Table.H>
#include <memory>
#include <vector>

namespace syncspirit::fltk::config {

struct table_t : Fl_Table {
    using parent_t = Fl_Table;
    using input_value_setter_t = void (table_t::*)(const property_t &);
    using input_value_getter_t = std::string_view (table_t::*)(const property_t &);

    struct clip_guart_t {
        clip_guart_t(int col, int x, int y, int w, int h);
        ~clip_guart_t();
        int w;
    };

    struct cell_t {
        virtual ~cell_t() = default;
        virtual clip_guart_t clip(int col, int x, int y, int w, int h);
        virtual void draw(int col, int x, int y, int w, int h) = 0;
    };

    using cell_ptr_t = std::unique_ptr<cell_t>;
    using cells_t = std::vector<cell_ptr_t>;

    table_t(categories_t categories, int x, int y, int w, int h);
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

  private:
    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);
    void create_cells();

    void set_int(const property_t &);

    categories_t categories;
    cells_t cells;
    Fl_Widget *input_int;
};

} // namespace syncspirit::fltk::config
