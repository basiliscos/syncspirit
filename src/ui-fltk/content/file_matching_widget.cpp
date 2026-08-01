// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "file_matching_widget.h"

#include "folder_widget.h"

#include "utils/format.hpp"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Table_Row.H>
#include <cstdint>

using namespace syncspirit;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

namespace {

static constexpr int PADDING = 5;
static constexpr int CELL_PADDING = 3;

using rows_t = std::vector<model::file_matcher_t>;

enum class hightlight_t { no, match, error };

} // namespace

struct file_matching_widget_t::table_t final : contentable_t<Fl_Table> {
    using parent_t = contentable_t<Fl_Table>;

    struct item_controls_t {
        item_controls_t() noexcept = default;
        item_controls_t(table_t *parent_, hightlight_t hightlight_, Fl_Group *tools_, Fl_Choice *match_mode_,
                        Fl_Input *input_) noexcept
            : parent{parent_}, hightlight{hightlight_}, tools{tools_}, match_mode{match_mode_}, input{input_} {}

        item_controls_t(const item_controls_t &) = delete;
        item_controls_t(item_controls_t &&other) noexcept { *this = std::move(other); }
        item_controls_t &operator=(item_controls_t &&other) noexcept {
            if (this != &other) {
                std::swap(parent, other.parent);
                std::swap(hightlight, other.hightlight);
                std::swap(tools, other.tools);
                std::swap(match_mode, other.match_mode);
                std::swap(input, other.input);
            }
            return *this;
        }

        ~item_controls_t() {
            if (parent) {
                if (tools) {
                    parent->remove(*tools);
                    delete tools;
                }
                if (match_mode) {
                    parent->remove(*match_mode);
                    delete match_mode;
                }
                if (input) {
                    parent->remove(*input);
                    delete input;
                }
            }
        }

        table_t *parent{nullptr};
        hightlight_t hightlight{hightlight_t::no};
        Fl_Group *tools{nullptr};
        Fl_Choice *match_mode{nullptr};
        Fl_Input *input{nullptr};
    };
    using controls_t = std::vector<item_controls_t>;

    table_t(file_matching_widget_t &container_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), container{container_} {
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

    ~table_t() { forget_controls(); }

    utils::logger_t &get_logger() noexcept { return container.container.container.supervisor.get_logger(); }

    void forget_controls() noexcept { controls.clear(); }

    Fl_Group *make_tools(const model::file_matcher_t &, int row) noexcept {
        static constexpr auto P = CELL_PADDING;
        int x, y, w, h;
        find_cell(CONTEXT_TABLE, row, 1, x, y, w, h);
        h = std::max(26, row_height(row));
        auto hh = h - P * 2;
        auto group = new Fl_Group(x, y, w, h);
        group->box(FL_FLAT_BOX);
        group->begin();
        auto rm = new Fl_Button(x + PADDING, y + P, hh, hh, "@undo");
        auto up = new Fl_Button(rm->x() + rm->w() + PADDING, y + P, hh, hh, "@<");
        auto down = new Fl_Button(up->x() + up->w() + PADDING, y + P, hh, hh, "@>");
        group->end();
        group->resizable(nullptr);

        rm->callback([](auto w, void *data) { reinterpret_cast<table_t *>(data)->on_rm(w); }, this);
        up->callback([](auto w, void *data) { reinterpret_cast<table_t *>(data)->on_move_up(w); }, this);
        down->callback([](auto w, void *data) { reinterpret_cast<table_t *>(data)->on_move_down(w); }, this);

        return group;
    }

    void on_rm(Fl_Widget *w) noexcept {
        auto tools = w->parent();
        if (items.size() > 1) {
            for (size_t i = 0; i < items.size(); ++i) {
                if (controls[i].tools == tools) {
                    auto it_c = controls.begin() + i;
                    controls.erase(it_c);
                    auto it_i = items.begin() + i;
                    items.erase(it_i);
                    break;
                }
            }
            rows(static_cast<int>(items.size()));
        } else {
            auto &c = controls[0];
            c.input->value("");
            c.hightlight = hightlight_t::no;
            c.match_mode->value(1);
        }
        redraw();
    }

    void on_move_up(Fl_Widget *w) noexcept {
        auto tools = w->parent();
        auto index = items.size();
        for (size_t i = 0; i < items.size(); ++i) {
            if (controls[i].tools == tools) {
                index = i;
                break;
            }
        }
        if (index > 0) {
            std::swap(items[index], items[index - 1]);
            refresh();
        }
        redraw();
    }

    void on_move_down(Fl_Widget *w) noexcept {
        using M = model::file_match_t;
        auto tools = w->parent();
        auto index = items.size();
        for (size_t i = 0; i < items.size(); ++i) {
            if (controls[i].tools == tools) {
                index = i;
                break;
            }
        }
        if (index + 1 < items.size()) {
            std::swap(items[index], items[index + 1]);
        } else {
            items.push_back(model::file_matcher_t(".*", M::accept));
            begin();
            auto item_controls = make_item_controls(items.back(), static_cast<int>(index + 1));
            end();
            controls.push_back(std::move(item_controls));
            rows(static_cast<int>(items.size()));
        }
        refresh();
        redraw();
    }

    Fl_Choice *make_mode(const model::file_matcher_t &item, int row) noexcept {
        int x, y, w, h;
        find_cell(CONTEXT_TABLE, row, 2, x, y, w, h);
        auto input = new Fl_Choice(x, y, w, h);
        input->add("off");
        input->add("accept");
        input->add("ignore");
        input->value(static_cast<int>(item.get_mode()));
        input->callback(
            [](Fl_Widget *self, void *data) {
                auto t = reinterpret_cast<table_t *>(data);
                for (std::size_t i = 0; i < t->controls.size(); ++i) {
                    if (t->controls[i].match_mode == self) {
                        auto &item = t->items[i];
                        auto value = static_cast<Fl_Choice *>(self)->value();
                        auto mode = static_cast<model::file_match_t>(value);
                        item.set_mode(mode);
                        auto [ec, off] = item.compile();
                        if (ec) {
                            auto &log = t->get_logger();
                            log->warn("cannot compile {} regex '{}': {}", i + 1, item.get_pattern(), ec);
                        }
                        break;
                    }
                }
                t->refresh();
                t->redraw();
            },
            this);
        return input;
    }

    Fl_Input *make_regex(const model::file_matcher_t &item, int row) noexcept {
        int x, y, w, h;
        find_cell(CONTEXT_TABLE, row, 3, x, y, w, h);
        auto input = new Fl_Input(x, y, w, h);
        input->value(item.get_pattern().data());
        input->callback(
            [](Fl_Widget *self, void *data) {
                auto t = reinterpret_cast<table_t *>(data);
                for (std::size_t i = 0; i < t->controls.size(); ++i) {
                    if (t->controls[i].input == self) {
                        auto &item = t->items[i];
                        auto data = std::string(static_cast<Fl_Input *>(self)->value());
                        item.set_pattern(data);
                        auto [ec, off] = item.compile();
                        if (ec) {
                            auto &log = t->get_logger();
                            log->warn("cannot compile {} regex '{}': {}", i + 1, item.get_pattern(), ec);
                        }
                        break;
                    }
                }
                t->refresh();
                t->redraw();
            },
            this);
        input->when(input->when() | FL_WHEN_CHANGED);
        return input;
    }

    item_controls_t make_item_controls(model::file_matcher_t &item, int row) {
        auto highlight = hightlight_t::no;
        auto [ec, off] = item.compile();
        if (ec) {
            auto &log = get_logger();
            log->warn("cannot compile {} regex '{}': {}", row + 1, item.get_pattern(), ec);
            highlight = hightlight_t::error;
        }
        auto tools = make_tools(item, row);
        auto type = make_mode(item, row);
        auto re = make_regex(item, row);
        return item_controls_t(this, highlight, tools, type, re);
    }

    void assing_rows(rows_t rows_) noexcept {
        begin();
        forget_controls();
        items = std::move(rows_);
        items.push_back(model::file_matcher_t());
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            auto item_controls = make_item_controls(items[i], i);
            controls.push_back(std::move(item_controls));
        }
        rows(static_cast<int>(items.size()));
        end();
        refresh();
        resize_widgets();
        redraw();
    }

    void assign_test(Fl_Input *test_input_) noexcept { test_input = test_input_; }

    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override {
        switch (context) {
        case CONTEXT_STARTPAGE: {
            fl_font(FL_HELVETICA, 16);
            return;
        }
        case CONTEXT_COL_HEADER: {
            fl_push_clip(x, y, w, h);
            fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
            fl_pop_clip();
            return;
        }
        case CONTEXT_CELL:
            if (col == 0) {
                draw_order(row, x, y, w, h);
            }
            return;
        case CONTEXT_RC_RESIZE: {
            update_col_widths(x, y, w, h);
            resize_widgets();
            return;
        }
        default:
            return;
        }
    }

    void resize_widgets() noexcept {
        for (int i = 0; i < rows(); ++i) {
            auto &control = controls.at(static_cast<size_t>(i));
            {
                int xx, yy, ww, hh;
                find_cell(CONTEXT_TABLE, i, 1, xx, yy, ww, hh);
                control.tools->resize(xx, yy, ww, hh);
                control.tools->redraw();
            }
            {
                int xx, yy, ww, hh;
                find_cell(CONTEXT_TABLE, i, 2, xx, yy, ww, hh);
                control.match_mode->resize(xx, yy, ww, hh);
                control.match_mode->redraw();
            }
            {
                int xx, yy, ww, hh;
                find_cell(CONTEXT_TABLE, i, 3, xx, yy, ww, hh);
                control.input->resize(xx, yy, ww, hh);
                control.input->redraw();
            }
        }
        init_sizes();
    }

    void update_col_widths(int x, int y, int w, int h) noexcept {
        auto w0 = std::max(60, col_width(0));
        // auto w1 = std::max(80, col_width(1));
        auto w1 = 80;
        auto w2 = std::max(85, col_width(2));
        auto w3 = std::max(1, tiw - (w0 + w1 + w2));

        col_width(0, w0);
        col_width(1, w1);
        col_width(2, w2);
        col_width(3, w3);
    }

    void resize(int x, int y, int w, int h) override {
        parent_t::resize(x, y, w, h);
        update_col_widths(x, y, w, h);
        init_sizes();
    }

    void refresh() override {
        using M = model::file_match_t;
        parent_t::refresh();

        auto test = test_input ? std::string_view(test_input->value()) : std::string_view();
        bool has_match{false};

        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            auto &item = items[i];
            auto &c = controls[i];
            auto mode = item.get_mode();
            c.match_mode->value(static_cast<int>(item.get_mode()));
            auto pattern = item.get_pattern();
            auto input_value = c.input->value();
            if (!input_value || input_value != pattern) {
                c.input->value(pattern.data());
            }
            if (mode == M::off) {
                c.hightlight = hightlight_t::no;
            } else if (!item.is_valid()) {
                c.hightlight = hightlight_t::error;
            } else {
                c.hightlight = hightlight_t::no;
                if (!has_match && test.size() && item.match(test) != M::off) {
                    c.hightlight = hightlight_t::match;
                    has_match = true;
                }
            }
        }
    }

    void draw_order(int row, int x, int y, int w, int h) {
        fl_push_clip(x, y, w, h);
        {
            using H = hightlight_t;
            auto text = fmt::format("{}", row + 1);
            fl_font(FL_HELVETICA, 16);
            Fl_Align align = FL_ALIGN_RIGHT;
            int dx = CELL_PADDING;

            auto &control = controls[row];
            auto highlight = control.hightlight == H::error   ? fl_rgb_color(255, 220, 220)
                             : control.hightlight == H::match ? fl_rgb_color(220, 255, 220)
                                                              : FL_WHITE;
            fl_color(highlight);
            fl_rectf(x, y, w, h);
            fl_color(FL_GRAY0);
            fl_draw(text.data(), x + dx, y, w - dx * 2, h, align);
            fl_color(color());
            fl_rect(x, y, w, h);
        }
        fl_pop_clip();
    }

    rows_t items;
    controls_t controls;
    file_matching_widget_t &container;
    Fl_Input *test_input{nullptr};
};

file_matching_widget_t::file_matching_widget_t(folder_widget_t &container_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), container{container_} {
    auto bottom_row = 30;
    using M = model::file_match_t;

    box(FL_FLAT_BOX);
    color(FL_DARK_GREEN);
    begin();

    table = new table_t(*this, x + PADDING, y + PADDING, w - PADDING * 2, h - (bottom_row + PADDING * 3));

    auto sample_rows = rows_t();
    sample_rows.push_back(model::file_matcher_t("^\\.DS_Store", M::ignore));
    sample_rows.push_back(model::file_matcher_t(".*\\.tmp", M::ignore));
    sample_rows.push_back(model::file_matcher_t(".secret$", M::ignore));
    sample_rows.push_back(model::file_matcher_t("^\\.env$", M::ignore));
    sample_rows.push_back(model::file_matcher_t("artefact", M::accept));
    sample_rows.push_back(model::file_matcher_t("error$\\", M::ignore));
    sample_rows.push_back(model::file_matcher_t("something", M::off));
    sample_rows.push_back(model::file_matcher_t(".*", M::accept));

    table->assing_rows(std::move(sample_rows));

    auto bottom = new Fl_Group(x + PADDING, table->y() + table->h() + PADDING, table->w(), bottom_row);
    bottom->box(FL_FLAT_BOX);
    bottom->color(FL_CYAN);

    bottom->begin();
    auto label = "Test area:";
    auto label_w = std::max(50, static_cast<int>(fl_width(label) + PADDING * 2));
    auto label_widget = new Fl_Box(bottom->x(), bottom->y(), label_w, bottom->h(), label);
    label_widget->box(FL_FLAT_BOX);
    label_widget->color(FL_DARK_RED);
    label_widget->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

    int ix = label_widget->x() + label_widget->w();
    int iw = (bottom->x() + bottom->w()) - ix;
    auto input = new Fl_Input(ix, bottom->y(), iw, bottom->h());
    bottom->end();
    bottom->resizable(input);
    end();

    table->assign_test(input);
    input->callback(
        [](auto, void *data) {
            auto t = reinterpret_cast<table_t *>(data);
            t->refresh();
            t->redraw();
        },
        table);
    input->when(input->when() | FL_WHEN_CHANGED);

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    auto sample_path = container.folder->get_path() / utils::make_generic_view("some/path/file.bin", allocator);
    auto path_str = sample_path.get_full_name();
    input->value(path_str.data());

    resizable(table);
    redraw();
}
