#include "log_table.h"
#include "log_sink.h"
#include "log_colors.h"
#include "log_utils.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <spdlog/sinks/sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/fmt/fmt.h>

namespace syncspirit::fltk {

static constexpr int col_min_size = 60;

log_table_t::log_table_t(displayed_records_t &displayed_records_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), displayed_records{displayed_records_}, auto_scrolling(true) {
    rows(0);            // how many rows
    row_header(0);      // enable row headers (along left)
    row_height_all(20); // default height of rows
    row_resize(0);      // disable row resizing
    // Cols
    cols(4);       // how many columns
    col_header(1); // enable column headers (along top)
    col_resize_min(col_min_size);
    col_width(0, 170);
    col_width(1, 100);
    col_width(2, 200);

    auto message_col_sz = this->w() - (col_width(0) + col_width(1) + col_width(2));
    col_width(3, message_col_sz);

    col_resize(1); // enable column resizing
    end();         // end the Fl_Table group

    // receive resize events
    when(FL_WHEN_CHANGED | when());
    set_visible_focus();
}

void log_table_t::update() {
    rows(displayed_records.size());
    if (auto_scrolling) {
        row_position(static_cast<int>(displayed_records.size()));
    }
}

void log_table_t::draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) {
    switch (context) {
    case CONTEXT_STARTPAGE:        // before page is drawn..
        fl_font(FL_HELVETICA, 16); // set the font for our drawing operations
        return;
    case CONTEXT_COL_HEADER: // Draw column headers
        draw_header(col, x, y, w, h);
        return;
    case CONTEXT_CELL: // Draw data in cells
        draw_data(row, col, x, y, w, h);
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

void log_table_t::draw_header(int col, int x, int y, int w, int h) {
    std::string_view label = col == 0 ? "time" : col == 1 ? "thread" : col == 2 ? "source" : "message";
    fl_push_clip(x, y, w, h);
    {
        fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
        fl_color(FL_BLACK);
        fl_draw(label.data(), x, y, w, h, FL_ALIGN_CENTER);
    }
    fl_pop_clip();
}

void log_table_t::draw_data(int row, int col, int x, int y, int w, int h) {
    auto &record = *displayed_records.at(static_cast<size_t>(row));
    std::string *content;
    fl_push_clip(x, y, w, h);
    {
        Fl_Align align = FL_ALIGN_CENTER;
        int x_offset = 0;
        int w_adj = 0;
        switch (col) {
        case 0:
            content = &record.date;
            break;
        case 1:
            content = &record.thread_id;
            align = FL_ALIGN_RIGHT;
            w_adj = 10;
            break;
        case 2:
            content = &record.source;
            break;
        default:
            content = &record.message;
            align = FL_ALIGN_LEFT;
            x_offset = 10;
            break;
        }

        auto color_row = row % 2;
        auto color_index = static_cast<int>(record.level) * 2;

        fl_color(row_selected(row) ? table_selection_color : log_colors[color_index + color_row]);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(content->data(), x + x_offset, y, w - x_offset - w_adj, h, align);
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}

void log_table_t::autoscroll(bool value) {
    auto_scrolling = value;
    if (auto_scrolling) {
        row_position(static_cast<int>(displayed_records.size()));
    }
}

std::string log_table_t::gather_selected() {
    std::stringstream buff;
    size_t count = 0;
    for (size_t i = 0; i < displayed_records.size(); ++i) {
        if (row_selected(static_cast<int>(i))) {
            if (count) {
                buff << eol;
            };
            ++count;
            auto &row = displayed_records.at(i);
            write(buff, *row);
        }
    }
    return buff.str();
}

int log_table_t::handle(int event) {
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

} // namespace syncspirit::fltk
