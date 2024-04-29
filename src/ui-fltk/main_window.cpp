#include "main_window.h"
#include "log_panel.h"
#include <FL/Fl_Button.H>

using namespace syncspirit::fltk;

main_window_t::main_window_t(application_t &application_)
    : parent_t(640, 480, "syncspirit-fltk"), application{application_} {
    int padding = 5;

    log_panel = new log_panel_t(application, padding, padding, w() - padding * 2, h() - padding * 2);
    end();

    resizable(this);
}

main_window_t::~main_window_t() { delete log_panel; }
