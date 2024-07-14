#include "static_table.h"

#include "log_colors.h"
#include "log_utils.h"
#include <FL/fl_draw.H>
#include <algorithm>
#include <sstream>

using namespace syncspirit::fltk;

static constexpr int PADDING = 5;

widgetable_t::widgetable_t(tree_item_t &container_) : container{container_}, widget{nullptr} {}

Fl_Widget *widgetable_t::get_widget() { return widget; }

void widgetable_t::reset() {}

bool widgetable_t::store(void *) { return true; }

static void resize_value(const std::string &, int, int, int, int) {}
static void resize_value(widgetable_ptr_t &widget, int x, int y, int w, int h) {
    auto impl = widget->get_widget();
    if (impl) {
        impl->resize(x, y, w, h);
        impl->redraw();
    };
}

static void reset_widget(std::string &) {}
static void reset_widget(widgetable_ptr_t &widget) { widget->reset(); }

static bool store_widget(std::string &, void *) { return true; }
static bool store_widget(widgetable_ptr_t &widget, void *data) { return widget->store(data); }

static int handle_value(const std::string &, int) { return 0; }
static int handle_value(widgetable_ptr_t &widget, int event) {
    auto impl = widget->get_widget();
    return impl ? impl->handle(event) : 0;
}

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
    create_widgets();
    reset();
    resize(x, y, w, h);
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
        row_height_all(27);
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
        for (int i = 0; i < rows(); ++i) {
            auto &row = table_rows.at(static_cast<size_t>(i));
            std::visit(
                [&](auto &&arg) {
                    int xx, yy, ww, hh;
                    find_cell(CONTEXT_TABLE, i, 1, xx, yy, ww, hh);
                    resize_value(arg, xx, yy, ww, hh);
                },
                row.value);
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
}

auto static_table_t::calc_col_widths() -> col_sizes_t {
    fl_font(FL_HELVETICA, 16);
    col_sizes_t r = {0, 0, 0, 0};
    for (auto &row : table_rows) {
        int x, y, w, h;
        fl_text_extents(row.label.data(), x, y, w, h);
        r.w1_min = std::max(r.w1_min, w + PADDING * 2);
        std::visit([&](auto &&arg) { calc_dimensions(arg, x, y, w, h); }, row.value);
        r.w2_min = std::max(r.w2_min, w + PADDING * 2);
    }
    r.w1 = r.w1_min;
    r.w2 = r.w2_min;
    auto w = tiw;
    auto delta = w - (r.w1 + r.w2);
    if (r.w1 + r.w2 < w) {
        auto tmp_w1_1 = r.w1 + static_cast<int>(delta * (double(r.w1_min) / r.w2_min));
        auto tmp_w1_2 = r.w1_min * 3 / 2;
        r.w1 = std::min(tmp_w1_1, tmp_w1_2);
        r.w2 = w - r.w1;
    }
    r.invalid = r.w1_min == PADDING * 2;
    return r;
}

void static_table_t::draw_data(int r, int col, int x, int y, int w, int h) {
    auto &row = table_rows.at(static_cast<size_t>(r));
    bool selected = row_selected(r);
    if (col == 0) {
        draw_label(row.label, selected, x, y, w, h);
    } else {
        std::visit([&](auto &&arg) { draw_value(arg, selected, x, y, w, h); }, row.value);
    }
}

void static_table_t::update_value(std::size_t row, std::string value) {
    auto &v = table_rows.at(row).value;
    assert(std::get_if<std::string>(&v));
    v = std::move(value);
    redraw_range(row, row, 1, 1);
}

auto static_table_t::get_rows() -> table_rows_t & { return table_rows; }

std::string static_table_t::gather_selected() {
    std::stringstream buff;
    size_t count = 0;
    for (size_t i = 0; i < table_rows.size(); ++i) {
        if (row_selected(static_cast<int>(i))) {
            auto &row = table_rows.at(i);
            auto value = std::get_if<std::string>(&row.value);
            if (value) {
                if (count) {
                    buff << eol;
                };
                ++count;
                buff << row.label << "\t" << *value;
            }
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

    // unselect rows with custom widgets
    for (size_t i = 0; i < table_rows.size(); ++i) {
        auto custom = std::get_if<widgetable_ptr_t>(&table_rows[i].value);
        if (custom && row_selected(i)) {
            select_row(i, 0);
        }
    }

    return r;
}

void static_table_t::draw_text(const std::string &text, bool selected, int x, int y, int w, int h) {
    fl_push_clip(x, y, w, h);
    {
        fl_font(FL_HELVETICA, 16);
        Fl_Align align = FL_ALIGN_LEFT;
        int dx = PADDING;
        int dw = 0;

        fl_color(selected ? table_selection_color : FL_WHITE);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(text.data(), x + dx, y, w + dw, h, align);
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}

void static_table_t::calc_dimensions(const std::string &str, int &x, int &y, int &w, int &h) {
    fl_text_extents(str.data(), x, y, w, h);
}

void static_table_t::calc_dimensions(const widgetable_ptr_t &, int &, int &, int &, int &) {}

void static_table_t::draw_label(const std::string &value, bool selected, int x, int y, int w, int h) {
    draw_text(value, selected, x, y, w, h);
}

void static_table_t::draw_value(const std::string &value, bool selected, int x, int y, int w, int h) {
    draw_text(value, selected, x, y, w, h);
}

void static_table_t::draw_value(const widgetable_ptr_t &, bool, int, int, int, int) {}

void static_table_t::create_widgets() {
    for (int i = 0; i < static_cast<int>(table_rows.size()); ++i) {
        auto &row = table_rows[i];
        std::visit([&](auto &&arg) { make_widget(arg, i); }, row.value);
    }
}

void static_table_t::make_widget(const std::string &, int) {}

void static_table_t::make_widget(const widgetable_ptr_t &w, int row) {
    int xx, yy, ww, hh;
    find_cell(CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
    auto backend = w->create_widget(xx, yy, ww, hh);
    add(backend);
}

void static_table_t::reset() {
    for (int i = 0; i < static_cast<int>(table_rows.size()); ++i) {
        auto &row = table_rows[i];
        std::visit([&](auto &arg) { reset_widget(arg); }, row.value);
    }
}

bool static_table_t::store(void *data) {
    bool ok = true;
    for (int i = 0; ok && i < static_cast<int>(table_rows.size()); ++i) {
        auto &row = table_rows[i];
        ok = std::visit([&](auto &arg) { return store_widget(arg, data); }, row.value);
    }
    return ok;
}
