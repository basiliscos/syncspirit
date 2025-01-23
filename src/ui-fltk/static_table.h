// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "content.h"
#include <FL/Fl_Table_Row.H>

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <string>
#include <vector>
#include <utility>
#include <variant>

namespace syncspirit::fltk {

struct widgetable_t : boost::intrusive_ref_counter<widgetable_t, boost::thread_unsafe_counter> {
    widgetable_t(Fl_Widget &container);
    virtual ~widgetable_t();

    virtual Fl_Widget *create_widget(int x, int y, int w, int h) = 0;
    Fl_Widget *get_widget();
    virtual void reset();
    virtual bool store(void *);

    Fl_Widget &container;
    Fl_Widget *widget = nullptr;
};

using widgetable_ptr_t = boost::intrusive_ptr<widgetable_t>;

struct string_provider_t : boost::intrusive_ref_counter<string_provider_t, boost::thread_unsafe_counter> {
    string_provider_t() = default;
    virtual ~string_provider_t() = default;

    virtual std::string_view value() const = 0;
};
using string_provider_ptr_t = boost::intrusive_ptr<string_provider_t>;

struct static_string_provider_t : string_provider_t {
    static_string_provider_t(std::string_view value = {});
    void update(std::string value);
    void update(std::string_view value);

    std::string_view value() const override;

  private:
    std::string str;
};
using static_string_provider_ptr_t = boost::intrusive_ptr<static_string_provider_t>;

using value_provider_t = std::variant<string_provider_ptr_t, widgetable_ptr_t>;

struct table_row_t {
    std::string label;
    value_provider_t value;

    table_row_t() = default;

    template <typename L, typename V>
    table_row_t(L &&label_, V &&value_) : label{std::forward<L>(label_)}, value{std::forward<V>(value_)} {}
};

using table_rows_t = std::vector<table_row_t>;

struct static_table_t : contentable_t<Fl_Table_Row> {
    using parent_t = contentable_t<Fl_Table_Row>;

    struct col_sizes_t {
        int w1;
        int w2;
        int w1_min;
        int w2_min;
        bool invalid;
    };

    static_table_t(int x, int y, int w, int h, table_rows_t &&rows = {});

    void assign_rows(table_rows_t rows);
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override;
    int handle(int event) override;
    void resize(int x, int y, int w, int h) override;
    table_rows_t &get_rows();
    void remove_row(widgetable_t &);
    void remove_row(int index);
    int find_row(const widgetable_t &);
    void insert_row(std::string_view label, value_provider_t, size_t index);

    void reset() override;
    bool store(void *) override;

  private:
    void create_widgets();
    void draw_data(int row, int col, int x, int y, int w, int h);
    void draw_label(const std::string &, bool selected, int x, int y, int w, int h);
    void draw_value(const string_provider_ptr_t &, bool selected, int x, int y, int w, int h);
    void draw_value(const widgetable_ptr_t &, bool selected, int x, int y, int w, int h);
    void calc_dimensions(const string_provider_ptr_t &, int &x, int &y, int &w, int &h);
    void calc_dimensions(const widgetable_ptr_t &, int &x, int &y, int &w, int &h);
    void draw_text(const std::string_view &, bool selected, int x, int y, int w, int h);
    void make_widget(const string_provider_ptr_t &, int row);
    void make_widget(const widgetable_ptr_t &, int row);
    void resize_widgets();

    std::string gather_selected();
    col_sizes_t calc_col_widths();

    table_rows_t table_rows;
};

} // namespace syncspirit::fltk
