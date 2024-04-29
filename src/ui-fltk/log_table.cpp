#include "log_table.h"
#include "log_sink.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <spdlog/sinks/sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/fmt/fmt.h>
#include <array>

namespace syncspirit::fltk {

static constexpr int col_min_size = 60;

using color_array_t = std::array<Fl_Color, 12>;

static color_array_t log_colors = {
    fl_rgb_color(220, 255, 220), fl_rgb_color(240, 255, 240), // trace
    fl_rgb_color(210, 245, 255), fl_rgb_color(230, 250, 255), // debug
    fl_rgb_color(245, 245, 245), fl_rgb_color(255, 255, 255), // info
    fl_rgb_color(255, 250, 200), fl_rgb_color(255, 250, 220), // warn
    fl_rgb_color(255, 220, 220), fl_rgb_color(255, 240, 240), // error
    fl_rgb_color(255, 220, 255), fl_rgb_color(255, 240, 255)  // critical
};

struct fltk_sink_t final : base_sink_t {
    fltk_sink_t(log_table_t *widget_) : widget{widget_} {}

    void forward(log_record_ptr_t record) override {
        auto lock = std::unique_lock(widget->incoming_mutex);
        widget->incoming_records.push_back(std::move(record));
        bool size = widget->incoming_records.size();
        lock.unlock();

        if (size == 1) {
            Fl::awake(
                [](void *data) {
                    auto widget = reinterpret_cast<log_table_t *>(data);
                    auto lock = std::unique_lock(widget->incoming_mutex);
                    auto &source = widget->incoming_records;
                    auto &dest = widget->records;
                    std::move(begin(source), end(source), std::back_insert_iterator(dest));
                    source.clear();
                    widget->update();
                    lock.unlock();
                },
                widget);
        }
    }

    log_table_t *widget;
};

log_table_t::log_table_t(application_t &application_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), application{application_}, auto_scrolling(true) {
    rows(0);            // how many rows
    row_header(0);      // enable row headers (along left)
    row_height_all(20); // default height of rows
    row_resize(0);      // disable row resizing
    // Cols
    cols(4);       // how many columns
    col_header(1); // enable column headers (along top)
    col_resize_min(col_min_size);
    col_width(0, 280);
    col_width(1, 100);
    col_width(2, 200);

    auto message_col_sz = this->w() - (col_width(0) + col_width(1) + col_width(2));
    col_width(3, message_col_sz);

    col_resize(1); // enable column resizing
    end();         // end the Fl_Table group

    bridge_sink = sink_ptr_t(new fltk_sink_t(this));

    auto &dist_sink = application.dist_sink;
    for (auto &sink : dist_sink->sinks()) {
        auto in_memory_sink = dynamic_cast<im_memory_sink_t *>(sink.get());
        if (in_memory_sink) {
            std::lock_guard lock(in_memory_sink->mutex);
            records = std::move(in_memory_sink->records);
            dist_sink->remove_sink(sink);
            break;
        }
    }

    dist_sink->add_sink(bridge_sink);

    // receive resize events
    when(FL_WHEN_CHANGED | when());
}

log_table_t::~log_table_t() {
    application.dist_sink->remove_sink(bridge_sink);
    bridge_sink.reset();
}

void log_table_t::update() {
    rows(records.size());
    if (auto_scrolling) {
        row_position(static_cast<int>(records.size()));
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
    std::string_view label = col == 0 ? "date" : col == 1 ? "thread" : col == 2 ? "source" : "message";
    fl_push_clip(x, y, w, h);
    {
        fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
        fl_color(FL_BLACK);
        fl_draw(label.data(), x, y, w, h, FL_ALIGN_CENTER);
    }
    fl_pop_clip();
}

void log_table_t::draw_data(int row, int col, int x, int y, int w, int h) {
    auto &record = *records.at(static_cast<size_t>(row));
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

        fl_color(log_colors[color_index + color_row]);
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
        row_position(static_cast<int>(records.size()));
    }
}

} // namespace syncspirit::fltk
