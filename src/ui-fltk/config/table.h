// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "category.h"
#include <FL/Fl_Table.H>
#include <memory>
#include <vector>

namespace syncspirit::fltk::config {

struct table_t : Fl_Table {
    using parent_t = Fl_Table;

    struct clip_guard_t {
        clip_guard_t(int col, int x, int y, int w, int h);
        ~clip_guard_t();
        int w;
    };

    struct cell_t {
        virtual ~cell_t() = default;
        virtual clip_guard_t clip(int col, int x, int y, int w, int h);
        virtual void draw(int col, int x, int y, int w, int h) = 0;
        virtual void resize(int x, int y, int w, int h);
        virtual void load_value();
    };

    using cell_ptr_t = std::unique_ptr<cell_t>;
    using cells_t = std::vector<cell_ptr_t>;
    using parent_t::find_cell;
    using parent_t::tih;
    using parent_t::tiy;

    table_t(const categories_t &categories, int x, int y, int w, int h);
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;

    void draw_header(int col, int x, int y, int w, int h);
    void draw_data(int row, int col, int x, int y, int w, int h);
    void create_cells();
    void reload_values();
    void done_editing();

    categories_t categories;
    cells_t cells;
};

} // namespace syncspirit::fltk::config
