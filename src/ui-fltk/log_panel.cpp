#include "log_panel.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <spdlog/sinks/sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/fmt/fmt.h>
#include <atomic>
#include <array>

using namespace syncspirit::fltk;

using log_level_t = spdlog::level_t;

struct log_panel_t::log_record_t {
    log_level_t level;
    std::string date;
    std::string source;
    std::string message;
    std::string thread_id;
};

namespace {

std::atomic_bool destroyed{false};

struct fltk_sink_t final : spdlog::sinks::sink {
    fltk_sink_t(log_panel_t *widget_) : widget{widget_}, date_formatter("%Y-%m-%d %H:%M:%S.%F") {}

    void log(const spdlog::details::log_msg &msg) override {
        struct context_t {
            log_panel_t *widget;
            log_panel_t::log_record_ptr_t record;
        };

        spdlog::memory_buf_t formatted;
        date_formatter.format(msg, formatted);
        auto date = std::string(formatted.begin(), formatted.end() - 1);
        auto source = msg.logger_name;
        auto message = msg.payload;

        auto record = std::make_unique<log_panel_t::log_record_t>(
            msg.level, std::move(date), std::string(source.begin(), source.end()),
            std::string(message.begin(), message.end()), std::to_string(msg.thread_id));

        Fl::awake(
            [](void *data) {
                auto context = reinterpret_cast<context_t *>(data);
                auto record = std::move(context->record);
                if (!destroyed) {
                    context->widget->append(std::move(record));
                }
                delete context;
            },
            new context_t(widget, std::move(record)));
    }

    void flush() override {}
    void set_pattern(const std::string &) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

    log_panel_t *widget;
    spdlog::pattern_formatter date_formatter;
};

} // namespace

log_panel_t::log_panel_t(utils::dist_sink_t dist_sink_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), dist_sink{dist_sink_} {
    rows(0);            // how many rows
    row_header(0);      // enable row headers (along left)
    row_height_all(20); // default height of rows
    row_resize(0);      // disable row resizing
    // Cols
    cols(4);       // how many columns
    col_header(1); // enable column headers (along top)
    col_width(0, 280);
    col_width(1, 100);
    col_width(2, 200);
    col_width(3, 400);
    col_resize(1); // enable column resizing
    end();         // end the Fl_Table group

    bridge_sink = sink_ptr_t(new fltk_sink_t(this));
    dist_sink->add_sink(bridge_sink);
}

log_panel_t::~log_panel_t() {
    dist_sink->remove_sink(bridge_sink);
    destroyed = true;
}

void log_panel_t::append(log_record_ptr_t record) {
    rows(rows() + 1);
    records.emplace_back(std::move(record));
}

void log_panel_t::draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) {
    switch (context) {
    case CONTEXT_STARTPAGE:        // before page is drawn..
        fl_font(FL_HELVETICA, 16); // set the font for our drawing operations
        return;
    case CONTEXT_COL_HEADER: // Draw column headers
        // sprintf(s,"%c",'A'+COL);                // "A", "B", "C", etc.
        // DrawHeader(s,X,Y,W,H);
        draw_header(col, x, y, w, h);
        return;
    case CONTEXT_CELL: // Draw data in cells
        draw_data(row, col, x, y, w, h);
        // sprintf(s,"%d",data[ROW][COL]);
        // DrawData(s,X,Y,W,H);
        return;
    default:
        return;
    }
}

void log_panel_t::draw_header(int col, int x, int y, int w, int h) {
    std::string_view label = col == 0 ? "date" : col == 1 ? "thread" : col == 2 ? "source" : "message";
    fl_push_clip(x, y, w, h);
    {
        fl_draw_box(FL_THIN_UP_BOX, x, y, w, h, row_header_color());
        fl_color(FL_BLACK);
        fl_draw(label.data(), x, y, w, h, FL_ALIGN_CENTER);
    }
    fl_pop_clip();
}

void log_panel_t::draw_data(int row, int col, int x, int y, int w, int h) {
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

        fl_color(FL_WHITE);
        fl_rectf(x, y, w, h);
        fl_color(FL_GRAY0);
        fl_draw(content->data(), x + x_offset, y, w - x_offset - w_adj, h, align);
        fl_color(color());
        fl_rect(x, y, w, h);
    }
    fl_pop_clip();
}
