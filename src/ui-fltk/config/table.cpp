#include "table.h"
#include <FL/fl_draw.H>

using namespace syncspirit::fltk::config;

table_t::clip_guart_t::clip_guart_t(int col, int x, int y, int w_, int h) : w{w_} {
    if (w) {
        fl_push_clip(x, y, w, h);
    }
}

table_t::clip_guart_t::~clip_guart_t() {
    if (w) {
        fl_pop_clip();
    }
}

auto table_t::cell_t::clip(int col, int x, int y, int w, int h) -> clip_guart_t {
    return clip_guart_t(col, x, y, w, h);
}

struct category_cell_t final : table_t::cell_t {
    using parent_t = table_t::cell_t;

    category_cell_t(table_t *table_, category_ptr_t category_) : table{table_}, category{std::move(category_)} {}

    auto clip(int col, int x, int y, int w, int h) -> table_t::clip_guart_t override {
        return col == 0 ? parent_t::clip(col, x, y, w, h) : table_t::clip_guart_t(0, 0, 0, 0, 0);
    }

    void draw(int col, int x, int y, int w, int h) override {
        Fl_Align align = FL_ALIGN_LEFT;
        const char *data = nullptr;

        bool restore_font = false;
        int offset = 5;
        int dw = 0;
        int text_dw = 0;
        auto bg_color = FL_GRAY0;
        switch (col) {
        case 0:
            data = category->get_label().data();
            break;
        case 1:
            fl_font(FL_HELVETICA, 14);
            data = category->get_explanation().data();
            dw = table->col_width(2) + table->col_width(3);
            bg_color = FL_BLUE;
            align = FL_ALIGN_RIGHT;
            text_dw = -offset;
            restore_font = true;
            break;
        default:
            return;
        }

        fl_color(bg_color);
        fl_rectf(x, y, w + dw, h);
        fl_color(FL_WHITE);
        if (data) {
            fl_draw(data, x + offset, y, w - offset + dw + text_dw, h, align);
        }
        if (restore_font) {
            fl_font(FL_HELVETICA, 16);
        }
    }

    table_t *table;
    category_ptr_t category;
};

table_t::table_t(categories_t categories_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), categories{std::move(categories_)} {

    row_header(0);
    row_height_all(20);
    row_resize(0);
    cols(4);
    col_header(0);
    col_width(0, w / 6);
    col_width(1, w / 6);
    col_width(2, w / 6);
    col_width(3, w / 2);
    col_resize(1);
    end();
    when(FL_WHEN_CHANGED | when());
    resizable(this);

    create_cells();
    rows(static_cast<int>(cells.size()));
}

void table_t::draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) {
    switch (context) {
    case CONTEXT_STARTPAGE: {
        fl_font(FL_HELVETICA, 16);
        return;
    }
    case CONTEXT_CELL:
        draw_data(row, col, x, y, w, h);
        return;
    case CONTEXT_RC_RESIZE: {
        /*
        auto col_widths = calc_col_widths();
        col_width(0, col_widths.first);
        col_width(1, col_widths.second);
        */
        return;
    }
    default:
        return;
    }
}

void table_t::draw_data(int r, int col, int x, int y, int w, int h) {
    auto &cell = cells.at(static_cast<size_t>(r));
    auto guard = cell->clip(col, x, y, w, h);
    cell->draw(col, x, y, w, h);
}

void table_t::create_cells() {
    for (auto &c : categories) {
        cells.push_back(cell_ptr_t(new category_cell_t(this, c)));
    }
}
