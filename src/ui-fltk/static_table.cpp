// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "static_table.h"

#include "log_colors.h"
#include "log_utils.h"
#include <FL/fl_draw.H>
#include <algorithm>
#include <sstream>

using namespace syncspirit::fltk;

static constexpr int PADDING = 5;

widgetable_t::widgetable_t(Fl_Widget &container_) : container{container_}, widget{nullptr} {}

widgetable_t::~widgetable_t() {
    if (widget) {
        delete widget;
    }
}

Fl_Widget *widgetable_t::get_widget() { return widget; }

void widgetable_t::reset() {}

bool widgetable_t::store(void *) { return true; }

static void resize_value(const string_provider_ptr_t &, int, int, int, int) {}

static void resize_value(widgetable_ptr_t &widget, int x, int y, int w, int h) {
    auto impl = widget->get_widget();
    if (impl) {
        impl->resize(x, y, w, h);
        if (auto group = dynamic_cast<Fl_Group *>(impl); group) {
            group->init_sizes();
        }
        impl->redraw();
    };
}

static void reset_widget(string_provider_ptr_t &) {}
static void reset_widget(widgetable_ptr_t &widget) { widget->reset(); }

// static bool store_widget(string_provider_t *&, void *) { return true; }
static bool store_widget(widgetable_t *widget, void *data) { return widget->store(data); }

static_string_provider_t::static_string_provider_t(std::string_view value) : str{std::move(value)} {}
void static_string_provider_t::update(std::string value) { str = value; }
void static_string_provider_t::update(std::string_view value) { str = value; }
std::string_view static_string_provider_t::value() const { return str; }

static_table_t::static_table_t(int x, int y, int w, int h, table_rows_t &&rows_) : parent_t(x, y, w, h) {
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
    resize(x, y, w, h);
    assign_rows(std::move(rows_));
}

void static_table_t::assign_rows(table_rows_t rows_) {
    table_rows = std::move(rows_);
    if (table_rows.size()) {
        rows(table_rows.size());
        create_widgets();
        reset();
        auto x = this->x(), y = this->y(), w = this->w(), h = this->h();
        resize(x, y, w, h);
        redraw();
    }
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
        resize_widgets();
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
    auto w = tiw;
    if (table_rows.empty()) {
        auto w2 = w / 2;
        return {w2, w2, w2, w2, true};
    }

    col_sizes_t r = {0, 0, 0, 0, true};
    for (auto &row : table_rows) {
        int x = 0, y = 0, w = 0, h = 0;
        auto label = std::string_view(row.label.data());
        if (label.size()) {
            fl_text_extents(label.data(), x, y, w, h);
        }
        r.w1_min = std::max(r.w1_min, w + PADDING * 2);
        std::visit([&](auto &&arg) { calc_dimensions(arg, x, y, w, h); }, row.value);
        r.w2_min = std::max(r.w2_min, w + PADDING * 2);
    }
    r.w1 = r.w1_min;
    r.w2 = r.w2_min;
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
    if (col == 0) {
        draw_label(row.label, x, y, w, h);
    } else {
        std::visit([&](auto &&arg) { draw_value(arg, x, y, w, h); }, row.value);
    }
}

auto static_table_t::get_rows() -> table_rows_t & { return table_rows; }

void static_table_t::draw_text(const std::string_view &text, int x, int y, int w, int h) {
    fl_push_clip(x, y, w, h);
    {
        fl_font(FL_HELVETICA, 16);
        Fl_Align align = FL_ALIGN_LEFT;
        int dx = PADDING;
        int dw = 0;

        fl_color(FL_WHITE);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(text.data(), x + dx, y, w + dw, h, align);
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}

void static_table_t::calc_dimensions(const string_provider_ptr_t &provider, int &x, int &y, int &w, int &h) {
    auto str = provider->value();
    fl_text_extents(str.data(), x, y, w, h);
}

void static_table_t::calc_dimensions(const widgetable_ptr_t &, int &, int &, int &, int &) {}

void static_table_t::draw_label(const std::string &value, int x, int y, int w, int h) { draw_text(value, x, y, w, h); }

void static_table_t::draw_value(const string_provider_ptr_t &provider, int x, int y, int w, int h) {
    auto value = provider->value();
    draw_text(value, x, y, w, h);
}

void static_table_t::draw_value(const widgetable_ptr_t &, int, int, int, int) {}

void static_table_t::create_widgets() {
    for (int i = 0; i < static_cast<int>(table_rows.size()); ++i) {
        auto &row = table_rows[i];
        std::visit([&](auto &&arg) { make_widget(arg, i); }, row.value);
    }
}

void static_table_t::make_widget(const string_provider_ptr_t &, int) {}

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
        auto &value = table_rows[i].value;
        // unknown crash on win32/release if for string_provider, std::visit
        // is not used
        auto widget = std::get_if<widgetable_ptr_t>(&value);
        if (widget) {
            ok = store_widget(widget->get(), data);
        }
    }
    return ok;
}

int static_table_t::find_row(const widgetable_t &item) {
    for (int i = 0; i < static_cast<int>(table_rows.size()); ++i) {
        auto &row = table_rows[i];
        auto widget = std::get_if<widgetable_ptr_t>(&row.value);
        if (widget && widget->get() == &item) {
            return i;
        }
    }
    return -1;
}

void static_table_t::resize_widgets() {
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
}

void static_table_t::remove_row(int index) {
    if (index >= 0) {
        if (index != static_cast<int>(table_rows.size()) - 1) {
            table_rows[index].value = {};
            for (int j = index + 1; j < static_cast<int>(table_rows.size()); ++j) {
                table_rows[j - 1] = std::move(table_rows[j]);
            }
        }
        table_rows.resize(table_rows.size() - 1);
        rows(table_rows.size());
        resize_widgets();
    }
}

void static_table_t::remove_row(widgetable_t &item) { remove_row(find_row(item)); }

void static_table_t::insert_row(std::string_view label, value_provider_t w, size_t index) {
    assert(index <= table_rows.size());
    table_rows.resize(table_rows.size() + 1);
    for (size_t j = table_rows.size() - 1; j > index; --j) {
        table_rows[j] = std::move(table_rows[j - 1]);
    }
    table_rows[index] = table_row_t{label, w};
    rows(table_rows.size());
    std::visit([&](auto &&arg) { make_widget(arg, static_cast<int>(index)); }, w);
    resize_widgets();
    redraw();
}
