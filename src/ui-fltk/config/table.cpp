#include "table.h"
#include <FL/fl_draw.H>

using namespace syncspirit::fltk::config;

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

    rows(static_cast<int>(categories.size()));
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
    auto &c = categories.at(static_cast<size_t>(r));
    fl_push_clip(x, y, w, h);
    {
        Fl_Align align = FL_ALIGN_LEFT;
        const char *data = nullptr;

        int dw = 0;
        switch (col) {
        case 0:
            data = c->get_label().data();
            break;
        }

        fl_color(FL_GRAY0);
        fl_rectf(x, y, w, h);
        fl_color(FL_WHITE);
        if (data) {
            fl_draw(data, x, y, w, h, align);
        }
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}
