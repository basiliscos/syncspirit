#include "log_panel.h"

#include <FL/Fl_Toggle_Button.H>
#include "log_table.h"

using namespace syncspirit::fltk;

static void auto_scroll_toggle(Fl_Widget *widget, void *data) {
    auto button = static_cast<Fl_Toggle_Button *>(widget);
    auto log_table = reinterpret_cast<log_table_t *>(data);
    log_table->autoscroll(button->value());
}

log_panel_t::log_panel_t(application_t &application, int x, int y, int w, int h) : parent_t{x, y, w, h} {
    int padding = 5;
    bool auto_scroll = true;

    auto bottom_row = 30;
    auto log_table_h = h - (padding * 2 + bottom_row);
    auto log_table = new log_table_t(application, padding, padding, w - padding * 2, log_table_h);
    log_table->autoscroll(auto_scroll);
    auto button = new Fl_Toggle_Button(padding, log_table->h() + padding * 2, 40, bottom_row - padding, "auto-scroll");
    button->value(auto_scroll);
    end();

    button->callback(auto_scroll_toggle, log_table);

    resizable(log_table);
}
