// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "file_matching_widget.h"

#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>

using namespace syncspirit;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

enum class match_mode_t {
    accept, ignore, off
};


namespace {

static constexpr int PADDING = 5;
static constexpr int CELL_PADDING = 3;

struct item_t: model::arc_base_t<item_t> {
    using parent_t = model::arc_base_t<item_t>;
    item_t(match_mode_t mode_, std::string value_) noexcept : mode{mode_}, value{value_} {}
    match_mode_t mode;
    std::string value;
};
using item_ptr_t = model::intrusive_ptr_t<item_t>;

using rows_t = std::vector<item_ptr_t>;

}

struct file_matching_widget_t::table_t: contentable_t<Fl_Table> {
    using parent_t = contentable_t<Fl_Table>;

    struct item_controls_t {
        Fl_Input* input{nullptr};
    };
    using controls_t = std::vector<item_controls_t>;

    table_t(int x, int y, int w, int h): parent_t(x,y,w,h) {
        row_header(0);
        row_resize(0);
        cols(4);
        col_header(1);
        col_resize(1);
        end();
        when(FL_WHEN_CHANGED | when());
        resizable(this);

        set_visible_focus();
        resize(x, y, w, h);
    }

    ~table_t() {
        forget_controls();
    }

    void forget_controls() noexcept {
        for (auto& c: controls) {
            if (c.input) {
                parent_t::remove(*c.input);
            }
        }
        controls.clear();
    }

    void assing_rows(rows_t rows_) noexcept {
        begin();
        forget_controls();
        items = std::move(rows_);
        for(int i = 0; i < static_cast<int>(items.size()); ++i) {
            auto& item = items[i];
            auto input = [&]() -> Fl_Input* {
                 int x, y, w, h;
                 find_cell(CONTEXT_TABLE, i, 3, x, y, w, h);
                 return new Fl_Input(x + CELL_PADDING, y, w - CELL_PADDING * 2, h);
            }();
            input->value(item->value.data());
            auto item_controls = item_controls_t{input};
            controls.push_back(std::move(item_controls));
        }
        rows(static_cast<int>(items.size()));
        end();
        resize_widgets();
        redraw();
    }

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override {
        switch (context) {
        case CONTEXT_STARTPAGE: {
            fl_font(FL_HELVETICA, 16);
            return;
        }
        case CONTEXT_COL_HEADER:
        {
            fl_push_clip(x, y, w, h);
            fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
            fl_pop_clip();
            return;
        }
        case CONTEXT_CELL:
            if (col == 0) {
                draw_order(row, x, y, w, h);
            } else if (col == 1) {
                draw_controls(row, x, y, w, h);
            } else if (col == 2) {
                draw_type(row, x, y, w, h);
            } else if (col == 3) {
                draw_regex(row, x, y, w, h);
            }
            return;
        case CONTEXT_RC_RESIZE: {
#if 0
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
            resize_widgets();
#endif
            return;
        }
        default:
            return;
        }
    }

    void resize_widgets() noexcept {
        for (int i = 0; i < rows(); ++i) {
            auto &control = controls.at(static_cast<size_t>(i));
            int xx, yy, ww, hh;
            find_cell(CONTEXT_TABLE, i, 3, xx, yy, ww, hh);
            control.input->resize(xx, yy, ww, hh);
            control.input->redraw();
        }
        init_sizes();
    }

    void resize(int x, int y, int w, int h) override {
        parent_t::resize(x, y, w, h);
        auto w0 = 60;
        auto w1 = 60;
        auto w2 = 85;
        auto w3 = std::max(1, tiw - (w0 + w1 + w2));

        col_width(0, w0);
        col_width(1, w1);
        col_width(2, w2);
        col_width(3, w3);

        init_sizes();
    }

    void draw_order(int row, int x, int y, int w, int h) {
        fl_push_clip(x, y, w, h);
        {
            auto text = fmt::format("{}", row + 1);
            fl_font(FL_HELVETICA, 16);
            Fl_Align align = FL_ALIGN_RIGHT;
            int dx = CELL_PADDING;

            fl_color(FL_WHITE);
            fl_rectf(x, y, w, h);
            fl_color(FL_GRAY0);
            fl_draw(text.data(), x + dx, y, w - dx * 2, h, align);
            fl_color(color());
            fl_rect(x, y, w, h);
        }
        fl_pop_clip();
    }

    void draw_controls(int row, int x, int y, int w, int h) {
    }

    void draw_type(int row, int x, int y, int w, int h) {
    }

    void draw_regex(int row, int x, int y, int w, int h) {
    }

    rows_t items;
    controls_t controls;
};

file_matching_widget_t::file_matching_widget_t(tree_item_t &container_, int x, int y, int w, int h):
    parent_t(x,y,w,h),
    container{container_}
{
    auto bottom_row = 30;

    box(FL_FLAT_BOX);
    color(FL_DARK_GREEN);
    begin();

    table = new table_t(x + PADDING, y + PADDING, w - PADDING * 2, h - (bottom_row + PADDING * 3));

    auto sample_rows = rows_t {
        new item_t(match_mode_t::accept, "artefact"),
        new item_t(match_mode_t::ignore, "^\\.DS_Store"),
        new item_t(match_mode_t::ignore, "*.tmp"),
        new item_t(match_mode_t::ignore, "*.secret"),
        new item_t(match_mode_t::ignore, "^\\.env$"),
        new item_t(match_mode_t::off, "something"),
        new item_t(match_mode_t::accept, ".*"),
    };
    table->assing_rows(std::move(sample_rows));

    auto bottom = new Fl_Group(x + PADDING, table->y() + table->h() + PADDING, table->w() , bottom_row);
    bottom->box(FL_FLAT_BOX);
    bottom->color(FL_CYAN);

    bottom->begin();
    auto label = "Test area:";
    auto label_w = std::max(50, static_cast<int>(fl_width(label) + PADDING * 2));
    auto label_widget = new Fl_Box(bottom->x(), bottom->y(), label_w , bottom->h(), label);
    label_widget->box(FL_FLAT_BOX);
    label_widget->color(FL_DARK_RED);
    label_widget->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

    int ix = label_widget->x() + label_widget->w();
    int iw = (bottom->x() + bottom->w()) - ix;
    auto input = new Fl_Input(ix, bottom->y(), iw, bottom->h());
    bottom->end();
    bottom->resizable(input);
    end();

    resizable(table);
    redraw();
}
