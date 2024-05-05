#include "main_window.h"
#include "log_panel.h"
#include <FL/Fl_Box.H>

using namespace syncspirit::fltk;

main_window_t::main_window_t(application_t &application_)
    : parent_t(640, 480, "syncspirit-fltk"), application{application_} {
    int padding = 0;

    auto content = new Fl_Box(padding, padding, w() - padding * 2, (h() / 3 * 2) - padding * 2, "content");
    log_panel = new log_panel_t(application, padding, padding, w() - padding * 2, (h() / 3) - padding * 2);
    log_panel->position(0, (h() / 3 * 2));
    end();

    resizable(this);
}

main_window_t::~main_window_t() { delete log_panel; }
