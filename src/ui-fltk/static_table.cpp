#include "static_table.h"

#include "log_colors.h"
#include "log_utils.h"
#include <FL/fl_draw.H>
#include <algorithm>
#include <sstream>

using namespace syncspirit::fltk;

static constexpr int PADDING = 5;

static_table_t::static_table_t(table_rows_t &&rows_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), table_rows(std::move(rows_)) {
    // box(FL_ENGRAVED_BOX);
    row_header(0);
    row_height_all(20);
    row_resize(0);
    cols(2);
    col_header(1);
    col_resize(1);
    end();
    when(FL_WHEN_CHANGED | when());
    resizable(this);

    set_visible_focus();
    rows(table_rows.size());
}

void static_table_t::draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) {
    switch (context) {
    case CONTEXT_STARTPAGE: {
        fl_font(FL_HELVETICA, 16);
        auto col_widths = calc_col_widths();
        if (!col_widths.invalid) {
            auto col_min_size = std::min(col_widths.w1_min, col_widths.w2_min);
            if (col_width(0) < col_min_size || col_width(1) < col_min_size) {
                col_width(0, col_widths.w1);
                col_width(1, col_widths.w2);
            }
        }
        return;
    }
    case CONTEXT_COL_HEADER:
        fl_push_clip(x, y, w, h);
        fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
        fl_pop_clip();
        return;
    case CONTEXT_CELL:
        draw_data(row, col, x, y, w, h);
        return;
    case CONTEXT_RC_RESIZE: {
        auto w0 = col_width(0);
        auto col_widths = calc_col_widths();
        auto col_min_size = std::min(col_widths.w1_min, col_widths.w2_min);
        if (w0 < col_min_size) {
            col_width(0, col_min_size);
            w0 = col_min_size;
        }
        auto last_sz = this->tiw - w0;
        if (last_sz >= col_min_size) {
            col_width(1, last_sz);
        }
        init_sizes();
        return;
    }
    default:
        return;
    }
}

void static_table_t::resize(int x, int y, int w, int h) {
    parent_t::resize(x, y, w, h);
    auto col_widths = calc_col_widths();
    col_width(0, col_widths.w1);
    col_width(1, col_widths.w2);
    init_sizes();
    redraw();
}

auto static_table_t::calc_col_widths() -> col_sizes_t {
    fl_font(FL_HELVETICA, 16);
    col_sizes_t r = {0, 0, 0, 0};
    for (auto &row : table_rows) {
        int x, y, w, h;
        fl_text_extents(row.label.data(), x, y, w, h);
        r.w1_min = std::max(r.w1_min, w + PADDING * 2);
        fl_text_extents(row.value.data(), x, y, w, h);
        r.w2_min = std::max(r.w2_min, w + PADDING * 2);
    }
    r.w1 = r.w1_min;
    r.w2 = r.w2_min;
    auto w = tiw;
    auto delta = w - (r.w1 + r.w2);
    if (r.w1 + r.w2 < w) {
        r.w1 += delta * (double(r.w1_min) / r.w2_min);
        r.w2 = w - r.w1;
    }
    r.invalid = r.w1_min == PADDING * 2;
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
            // align = FL_ALIGN_RIGHT;
            content = &row.value;
            // dx = 0;
            // dw = -PADDING;
        }

        fl_color(row_selected(r) ? table_selection_color : FL_WHITE);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(content->data(), x + dx, y, w + dw, h, align);
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}

void static_table_t::update_value(std::size_t row, std::string value) {
    table_rows.at(row).value = std::move(value);
    redraw_range(row, row, 1, 1);
}

std::string static_table_t::gather_selected() {
    std::stringstream buff;
    size_t count = 0;
    for (size_t i = 0; i < table_rows.size(); ++i) {
        if (row_selected(static_cast<int>(i))) {
            if (count) {
                buff << eol;
            };
            ++count;
            auto &row = table_rows.at(i);
            buff << row.label << "\t" << row.value;
        }
    }
    return buff.str();
}

int static_table_t::handle(int event) {
    auto r = parent_t::handle(event);
    if (event == FL_RELEASE && r) {
        auto buff = gather_selected();
        if (buff.size()) {
            Fl::copy(buff.data(), buff.size(), 0);
        }
        take_focus();
    } else if (event == FL_KEYBOARD) {
        if ((Fl::event_state() & (FL_CTRL | FL_COMMAND)) && Fl::event_key() == 'c') {
            auto buff = gather_selected();
            if (buff.size()) {
                Fl::copy(buff.data(), buff.size(), 1);
            }
            r = 1;
        }
    } else if (event == FL_UNFOCUS) {
        select_all_rows(0);
        r = 1;
    }

    return r;
}
