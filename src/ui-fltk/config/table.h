#pragma once

#include "category.h"
#include <FL/Fl_Table.H>
#include <memory>
#include <vector>

namespace syncspirit::fltk::config {

struct table_t : Fl_Table {
    using parent_t = Fl_Table;
    using input_value_setter_t = void (table_t::*)(const property_t &);
    using input_value_saver_t = void (table_t::*)(property_t &);

    struct clip_guart_t {
        clip_guart_t(int col, int x, int y, int w, int h);
        ~clip_guart_t();
        int w;
    };

    struct cell_t {
        virtual ~cell_t() = default;
        virtual clip_guart_t clip(int col, int x, int y, int w, int h);
        virtual void draw(int col, int x, int y, int w, int h) = 0;
        virtual void resize(int col, int x, int y, int w, int h);
    };

    using cell_ptr_t = std::unique_ptr<cell_t>;
    using cells_t = std::vector<cell_ptr_t>;
    using parent_t::find_cell;
    using parent_t::tih;
    using parent_t::tiy;

    table_t(categories_t categories, int x, int y, int w, int h);
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);
    void create_cells();
    void on_callback();
    void try_start_editing(int row, int col);
    void done_editing();

    void set_int(const property_t &);
    void save_int(property_t &);

    void set_text(const property_t &);
    void save_text(property_t &);

    categories_t categories;
    cells_t cells;
    Fl_Widget *input_int;
    Fl_Widget *input_text;
    cell_t *currently_edited;
};

} // namespace syncspirit::fltk::config
