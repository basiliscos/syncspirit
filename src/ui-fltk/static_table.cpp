#include "static_table.h"

#include <FL/fl_draw.H>
#include <algorithm>

using namespace syncspirit::fltk;

static constexpr int PADDING = 5;

static_table_t::static_table_t(table_rows_t &&rows, int x, int y, int w, int h):
    parent_t(x, y, w, h), table_rows(std::move(rows)) {
    this->rows(table_rows.size());
    box(FL_ENGRAVED_BOX);
    row_header(0);
    row_height_all(20);
    row_resize(0);
    cols(2);
    col_header(0);
    col_width(0, w/2);
    col_width(1, w/2);
    col_resize(1);
    when(FL_WHEN_CHANGED | when());
    resizable(this);
}

void static_table_t::draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) {
    switch (context) {
    case CONTEXT_STARTPAGE: {
        fl_font(FL_HELVETICA, 16);
        auto col_widths = calc_col_widths();
        col_width(0, col_widths.first);
        col_width(1, col_widths.second);
        return;
    }
    case CONTEXT_CELL:
        draw_data(row, col, x, y, w, h);
        return;
    case CONTEXT_RC_RESIZE:{
        auto col_widths = calc_col_widths();
        col_width(0, col_widths.first);
        col_width(1, col_widths.second);
//        col_width(0, this->tiw/2);
//        col_width(1, this->tiw/2);
        return;
    }
    default:
        return;
    }
}

std::pair<int, int> static_table_t::calc_col_widths() {
    std::pair<int, int> r = {0, 0};
    for(auto& row: table_rows) {
        int x, y, w, h;
        fl_text_extents(row.label.data(), x, y, w, h);
        r.first = std::max(r.first, w + PADDING * 2);
        fl_text_extents(row.value.data(), x, y, w, h);
        r.second = std::max(r.second, w + PADDING * 2);
    }
    auto delta = tiw - (r.first + r.second);
    if (r.first + r.second < tiw) {
        r.first += delta/2;
        r.second += delta/2;
    }
    return r;
}


void static_table_t::draw_data(int r, int col, int x, int y, int w, int h) {
    auto &row = table_rows.at(static_cast<size_t>(r));
    std::string *content;
    fl_push_clip(x, y, w, h);
    {
        Fl_Align align = FL_ALIGN_LEFT;
        int dx = PADDING;
        int dw = 0;
        switch (col) {
        case 0:
            content = &row.label;
            break;
        case 1:
            align = FL_ALIGN_RIGHT;
            content = &row.value;
            dx = 0;
            dw = -PADDING;
        }

        fl_color(FL_WHITE);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(content->data(), x + dx, y, w + dw, h, align);
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}
