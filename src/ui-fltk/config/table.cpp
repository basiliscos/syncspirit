#include "table.h"

#include <FL/fl_draw.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
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

void table_t::cell_t::resize(int x, int y, int w, int h) {}

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
            text_dw = -offset;
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

struct property_cell_t : table_t::cell_t {
    using parent_t = table_t::cell_t;

    property_cell_t(table_t *table_, property_ptr_t property_, int row_)
        : table{table_}, property{std::move(property_)}, row{row_}, editing{false} {

        int xx, yy, ww, hh;
        table->find_cell(table_t::CONTEXT_TABLE, row, 2, xx, yy, ww, hh);
        group_buttons = new Fl_Group(xx, yy, ww, hh);
        group_buttons->box(FL_FLAT_BOX);

        group_buttons->begin();
        int padding = 2;
        auto button_w = ww / 2 - padding * 3;
        auto button_h = hh - padding * 2;

        button_undo = new Fl_Button(xx + padding, yy + padding, button_w, button_h, "@undo");
        button_reset = new Fl_Button(xx + padding + button_w + padding, yy + padding, button_w, button_h, "@refresh");

        group_buttons->end();
        group_buttons->resizable(group_buttons);

        update_buttons();
    }

    void draw(int col, int x, int y, int w, int h) override {
        switch (col) {
        case 0:
            draw_label(x, y, w, h);
            break;
        case 3:
            draw_explanation_or_error(x, y, w, h);
            break;
        }
    }

    void draw_label(int x, int y, int w, int h) {
        auto label = property->get_label();
        fl_color(FL_BLACK);
        fl_draw(label.data(), x, y, w, h, FL_ALIGN_LEFT);
    }

    void update_buttons() {
        if (property->same_as_initial()) {
            button_undo->deactivate();
        } else {
            button_undo->activate();
        }

        if (property->same_as_default()) {
            button_reset->deactivate();
        } else {
            button_reset->activate();
        }
    }

    void done_editing() {
        update_buttons();
        table->redraw();
    }

    void draw_explanation_or_error(int x, int y, int w, int h) {
        auto &err = property->validate();
        const char *text;
        if (err) {
            fl_color(FL_RED);
            fl_rectf(x, y, w, h);
            fl_color(FL_BLACK);
            text = err->c_str();
        } else {
            text = property->get_explanation().data();
        }
        fl_color(FL_BLACK);
        fl_draw(text, x, y, w, h, FL_ALIGN_LEFT);
    }

    void resize(int x, int y, int w, int h) override {
        [&]() -> void {
            int xx, yy, ww, hh;
            table->find_cell(table_t::CONTEXT_TABLE, row, 2, xx, yy, ww, hh);
            group_buttons->resize(xx, yy, ww, hh);
        }();
        [&]() -> void {
            int xx, yy, ww, hh;
            table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
            input_generic->resize(xx, yy, ww, hh);
        }();
    }

    table_t *table;
    property_ptr_t property;
    int row;
    bool editing;
    Fl_Widget *input_generic;
    Fl_Group *group_buttons;
    Fl_Button *button_undo;
    Fl_Button *button_reset;
};

struct int_cell_t final : property_cell_t {
    using parent_t = property_cell_t;

    int_cell_t(table_t *table_, property_ptr_t property_, int row_) : parent_t(table_, std::move(property_), row_) {
        int xx, yy, ww, hh;
        table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
        auto input = new Fl_Int_Input(xx, yy, ww, hh);
        input->value(property->get_value().data());
        input->callback([](Fl_Widget *, void *data) { reinterpret_cast<int_cell_t *>(data)->save_input(); }, this);
        input_generic = input;
    }

    void save_input() {
        auto value = static_cast<Fl_Int_Input *>(input_generic)->value();
        property->set_value(value);
        done_editing();
    }
};

struct string_cell_t final : property_cell_t {
    using parent_t = property_cell_t;

    string_cell_t(table_t *table_, property_ptr_t property_, int row_) : parent_t(table_, std::move(property_), row_) {
        int xx, yy, ww, hh;
        table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
        auto input = new Fl_Input(xx, yy, ww, hh);
        input->value(property->get_value().data());
        input_generic = input;
    }
};

table_t::table_t(categories_t categories_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), categories{std::move(categories_)} {

    row_header(0);
    row_height_all(20);
    row_resize(0);
    cols(4);
    col_header(1);
    col_width(0, 220);
    col_width(1, 570);
    col_width(2, 60);
    col_width(3, w / 2);
    col_resize(1);

    when(FL_WHEN_CHANGED | when());
    resizable(this);

    end();

    create_cells();
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

        for (size_t i = 0; i < cells.size(); ++i) {
            cells.at(i)->resize(x, y, w, h);
        }
        init_sizes();
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
    int total_rows = 0;
    for (auto &c : categories) {
        ++total_rows;
        for (auto &p : c->get_properties()) {
            ++total_rows;
        }
    }

    rows(total_rows);
    int row = 0;
    begin();
    for (auto &c : categories) {
        cells.push_back(cell_ptr_t(new category_cell_t(this, c)));
        row++;
        for (auto &p : c->get_properties()) {
            auto cell_ptr = [&]() -> property_cell_t * {
                if (p->get_kind() == property_kind_t::positive_integer) {
                    return new int_cell_t(this, p, row);
                } else if (p->get_kind() == property_kind_t::boolean) {
                    return new int_cell_t(this, p, row);
                } else if (p->get_kind() == property_kind_t::text) {
                    return new string_cell_t(this, p, row);
                }
                assert(0 && "should not happen");
            }();
            ++row;
            cells.push_back(cell_ptr_t(cell_ptr));
        }
    }
    end();
}
