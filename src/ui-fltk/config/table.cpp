#include "table.h"

#include <FL/fl_draw.H>
#include <FL/Fl_Int_Input.H>
#include <cassert>
#include <functional>

using namespace syncspirit::fltk::config;

static constexpr int col_min_size = 60;

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
        Fl_Align align = FL_ALIGN_RIGHT;
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

struct property_description_t {
    int row;
    Fl_Widget *widget;
    table_t::input_value_setter_t setter;
    table_t::input_value_saver_t saver;
};

struct property_cell_t final : table_t::cell_t {
    using parent_t = table_t::cell_t;

    property_cell_t(table_t *table_, property_ptr_t property_, property_description_t descr_)
        : table{table_}, property{std::move(property_)}, descr{descr_}, editing{false} {}

    void draw(int col, int x, int y, int w, int h) override {
        switch (col) {
        case 0:
            draw_label(x, y, w, h);
            break;
        case 1:
            draw_input(x, y, w, h);
            break;
        case 3:
            draw_explanation(x, y, w, h);
            break;
        }
    }

    void draw_label(int x, int y, int w, int h) {
        auto label = property->get_label();
        fl_color(FL_BLACK);
        fl_draw(label.data(), x, y, w, h, FL_ALIGN_LEFT);
    }

    void draw_input(int x, int y, int w, int h) {
        if (editing && descr.widget->visible()) {
            return;
        }
        auto value = property->get_value();
        fl_color(FL_WHITE);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(value.data(), x, y, w, h, FL_ALIGN_LEFT);
        fl_color(table->color());
        fl_rect(x, y, w, h);
    }

    void draw_explanation(int x, int y, int w, int h) {
        auto text = property->get_explanation();
        fl_color(FL_BLACK);
        fl_draw(text.data(), x, y, w, h, FL_ALIGN_LEFT);
    }

    void start_edit() {
        int x, y, w, h;
        table->find_cell(table_t::CONTEXT_CELL, descr.row, 1, x, y, w, h);
        auto widget = descr.widget;
        std::invoke(descr.setter, *table, *property);
        widget->resize(x, y, w, h);
        widget->show();
        widget->take_focus();
        table->currently_edited = this;
        editing = true;
        printf("start_edit\n");
    }

    void done_edit() {
        auto widget = descr.widget;
        if (widget->visible()) {
            widget->hide();
        }
        std::invoke(descr.saver, *table, *property);
        table->currently_edited = nullptr;
        editing = false;
        printf("done_edit\n");
    }

    table_t *table;
    property_ptr_t property;
    property_description_t descr;
    bool editing;
};

table_t::table_t(categories_t categories_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), categories{std::move(categories_)}, currently_edited{nullptr} {

    input_int = new Fl_Int_Input(0, 0, 0, 0);
    input_int->hide();

    row_header(0);
    row_height_all(20);
    row_resize(0);
    cols(4);
    col_header(1);
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

    callback([](Fl_Widget *w, void *data) { static_cast<table_t *>(w)->on_callback(); });
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
    case CONTEXT_COL_HEADER:
        draw_header(col, x, y, w, h);
        return;
    case CONTEXT_RC_RESIZE: {
        auto until_last = col_width(0) + col_width(1) + col_width(2);
        auto last_sz = this->tiw - until_last;
        auto delta = col_min_size - last_sz;
        if (last_sz >= col_min_size) {
            col_width(3, last_sz);
        }
        return;
    }
    default:
        return;
    }
}

void table_t::draw_header(int col, int x, int y, int w, int h) {
    fl_push_clip(x, y, w, h);
    fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
    fl_pop_clip();
}

void table_t::draw_data(int r, int col, int x, int y, int w, int h) {
    auto &cell = cells.at(static_cast<size_t>(r));
    auto guard = cell->clip(col, x, y, w, h);
    cell->draw(col, x, y, w, h);
}

void table_t::create_cells() {
    int row = 0;
    for (auto &c : categories) {
        cells.push_back(cell_ptr_t(new category_cell_t(this, c)));
        row++;
        for (auto &p : c->get_properties()) {
            auto descr = [&]() -> property_description_t {
                if (p->get_kind() == property_kind_t::positive_integer) {
                    return property_description_t{row++, input_int, &table_t::set_int, &table_t::save_int};
                }
                assert(0 && "should not happen");
            }();
            cells.push_back(cell_ptr_t(new property_cell_t(this, p, descr)));
        }
    }
}

void table_t::on_callback() {
    auto row = callback_row();
    auto col = callback_col();
    auto context = callback_context();

    switch (context) {
    case CONTEXT_CELL: {
        switch (Fl::event()) {
        case FL_PUSH:
            done_editing();
            if (row != rows() - 1 && col != cols() - 1)
                try_start_editing(row, col);
            return;
        }
    case CONTEXT_TABLE:      // A table event occurred on dead zone in table
    case CONTEXT_ROW_HEADER: // A table event occurred on row/column header
    case CONTEXT_COL_HEADER:
        done_editing(); // done editing, hide
        return;

    default:
        return;
    }
    }
}

void table_t::set_int(const property_t &property) {
    static_cast<Fl_Int_Input *>(input_int)->value(property.get_value().data());
}

void table_t::save_int(property_t &property) { property.set_value(static_cast<Fl_Int_Input *>(input_int)->value()); }

void table_t::try_start_editing(int row, int col) {
    if (col != 1) {
        return;
    }
    auto &cell = cells.at(row);
    auto property_cell = dynamic_cast<property_cell_t *>(cell.get());
    if (!property_cell) {
        return;
    }

    property_cell->start_edit();
}

void table_t::done_editing() {
    if (currently_edited) {
        auto cell = static_cast<property_cell_t *>(currently_edited);
        cell->done_edit();
    }
}
