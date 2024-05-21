#include "table.h"

#include <FL/fl_draw.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <boost/filesystem.hpp>
#include <spdlog/fmt/fmt.h>

#include <cassert>
#include <functional>

using namespace syncspirit::fltk::config;
namespace bfs = boost::filesystem;

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
void table_t::cell_t::load_value() {}

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
        button_undo->callback(
            [](Fl_Widget *, void *data) {
                auto self = static_cast<property_cell_t *>(data);
                self->property->undo();
                self->load_value();
            },
            this);
        button_undo->copy_tooltip("undo to initial");

        button_reset = new Fl_Button(xx + padding + button_w + padding, yy + padding, button_w, button_h, "@refresh");
        button_reset->callback(
            [](Fl_Widget *, void *data) {
                auto self = static_cast<property_cell_t *>(data);
                self->property->reset();
                self->load_value();
            },
            this);
        button_reset->copy_tooltip("reset to default");

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

    void load_value() override { done_editing(); }

    void done_editing() {
        update_buttons();
        table->done_editing();
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
        input->callback([](Fl_Widget *, void *data) { reinterpret_cast<int_cell_t *>(data)->save_input(); }, this);
        input_generic = input;
        load_value();
    }

    void load_value() {
        auto value = property->get_value();
        static_cast<Fl_Int_Input *>(input_generic)->value(value.data());
        parent_t::load_value();
    }

    void save_input() {
        auto value = static_cast<Fl_Int_Input *>(input_generic)->value();
        property->set_value(value);
        done_editing();
    }
};

struct bool_cell_t final : property_cell_t {
    using parent_t = property_cell_t;

    bool_cell_t(table_t *table_, property_ptr_t property_, int row_) : parent_t(table_, std::move(property_), row_) {
        int xx, yy, ww, hh;
        table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
        auto input = new Fl_Check_Button(xx, yy, ww, hh);
        input->callback([](Fl_Widget *, void *data) { reinterpret_cast<bool_cell_t *>(data)->save_input(); }, this);
        input_generic = input;
        load_value();
    }

    void load_value() override {
        auto value = property->get_value();
        static_cast<Fl_Check_Button *>(input_generic)->value(value == "true");
        parent_t::load_value();
    }

    void save_input() {
        auto value = static_cast<Fl_Check_Button *>(input_generic)->value();
        property->set_value(value ? "true" : "");
        done_editing();
    }
};

struct string_cell_t final : property_cell_t {
    using parent_t = property_cell_t;

    string_cell_t(table_t *table_, property_ptr_t property_, int row_) : parent_t(table_, std::move(property_), row_) {
        int xx, yy, ww, hh;
        table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
        auto input = new Fl_Input(xx, yy, ww, hh);
        input->callback([](Fl_Widget *, void *data) { reinterpret_cast<string_cell_t *>(data)->save_input(); }, this);
        input_generic = input;
        load_value();
    }

    void load_value() override {
        auto value = property->get_value();
        static_cast<Fl_Input *>(input_generic)->value(value.data());
        parent_t::load_value();
    }

    void save_input() {
        auto value = static_cast<Fl_Input *>(input_generic)->value();
        property->set_value(value);
        done_editing();
    }
};

struct path_cell_t final : property_cell_t {
    using parent_t = property_cell_t;

    path_cell_t(table_t *table_, property_ptr_t property_, int row_) : parent_t(table_, std::move(property_), row_) {
        int xx, yy, ww, hh;
        auto w = 25;
        auto p = 2;
        table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
        auto group = new Fl_Group(xx, yy, ww, hh);
        group->begin();
        auto input = new Fl_Input(xx, yy, ww - (w + p * 2), hh);
        input->callback([](Fl_Widget *, void *data) { reinterpret_cast<path_cell_t *>(data)->save_input(); }, this);

        auto button = new Fl_Button(xx + input->w() + p, yy + p, w, hh - p * 2, "...");
        button->callback([](Fl_Widget *, void *data) { reinterpret_cast<path_cell_t *>(data)->on_click(); }, this);

        group->end();
        input_generic = group;
        load_value();
    }

    void load_value() override {
        auto value = property->get_value();
        auto child_control = static_cast<Fl_Group *>(input_generic)->child(0);
        static_cast<Fl_Input *>(child_control)->value(value.data());
        parent_t::load_value();
    }

    void save_input() {
        auto child_control = static_cast<Fl_Group *>(input_generic)->child(0);
        auto value = static_cast<Fl_Input *>(child_control)->value();
        property->set_value(value);
        done_editing();
    }

    void on_click() {
        using T = Fl_Native_File_Chooser::Type;
        auto k = property->get_kind();
        auto type = k == property_kind_t::file ? T::BROWSE_FILE : T::BROWSE_DIRECTORY;

        Fl_Native_File_Chooser file_chooser;
        file_chooser.title(property->get_explanation().data());
        file_chooser.type(type);
        if (k == property_kind_t::file) {
            auto path = bfs::path(property->get_value());
            auto parent = path.parent_path().string();
            file_chooser.directory(parent.data());

            auto ext = fmt::format("*{}", path.extension().string());
            auto filter = fmt::format("{}\t{}", ext, ext);
            file_chooser.filter(filter.data());
        }

        auto r = file_chooser.show();
        if (r != 0) {
            return;
        }

        // commit
        property->set_value(file_chooser.filename());
        load_value();
        done_editing();
    }
};

struct log_cell_t final : property_cell_t {
    using parent_t = property_cell_t;

    log_cell_t(table_t *table_, property_ptr_t property_, int row_) : parent_t(table_, std::move(property_), row_) {
        group_buttons->hide();
        int xx, yy, ww, hh;
        table->find_cell(table_t::CONTEXT_TABLE, row, 1, xx, yy, ww, hh);
        auto input = new Fl_Input(xx, yy, ww, hh);
        input->deactivate();
        input_generic = input;
        load_value();
    }

    void load_value() override {
        auto value = property->get_value();
        static_cast<Fl_Input *>(input_generic)->value(value.data());
        parent_t::load_value();
    }
};

table_t::table_t(const categories_t &categories_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), categories{categories_} {

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
                auto k = p->get_kind();
                if (k == property_kind_t::positive_integer) {
                    return new int_cell_t(this, p, row);
                } else if (k == property_kind_t::boolean) {
                    return new bool_cell_t(this, p, row);
                } else if (k == property_kind_t::text) {
                    return new string_cell_t(this, p, row);
                } else if (k == property_kind_t::file || k == property_kind_t::directory) {
                    return new path_cell_t(this, p, row);
                } else if (k == property_kind_t::log_sink) {
                    return new log_cell_t(this, p, row);
                }
                assert(0 && "should not happen");
            }();
            ++row;
            cells.push_back(cell_ptr_t(cell_ptr));
        }
    }
    end();
}

void table_t::done_editing() {
    if (auto cb = callback(); cb) {
        Fl_Widget::do_callback();
    };
    redraw();
}

void table_t::reload_values() {
    for (auto &c : cells) {
        c->load_value();
    }
    redraw();
}
