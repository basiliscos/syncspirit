#include "main_window.h"
#include "log_panel.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Tile.H>

using namespace syncspirit::fltk;

main_window_t::main_window_t(application_t &application_)
    : parent_t(640, 480, "syncspirit-fltk"), application{application_} {
    int padding = 0;

    auto container = new Fl_Tile(padding, 0, w(), h());
    auto content = new Fl_Box(padding, padding, w() - padding * 2, (h() / 3 * 2) - padding * 2, "content");
    content->box(FL_FLAT_BOX);

    log_panel = new log_panel_t(application, padding, padding, w() - padding * 2, (h() / 3) - padding * 2);
    log_panel->position(0, (h() / 3 * 2));
    log_panel->box(FL_FLAT_BOX);

    container->end();

    end();

    resizable(this);
}

main_window_t::~main_window_t() { delete log_panel; }
