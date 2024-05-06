#include "main_window.h"

#include "log_panel.h"
#include "tree_view.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Tile.H>

using namespace syncspirit::fltk;

main_window_t::main_window_t(app_supervisor_t &supervisor_)
    : parent_t(700, 480, "syncspirit-fltk"), supervisor{supervisor_} {

    auto container = new Fl_Tile(0, 0, w(), h());
    auto content_w = w() / 2;
    auto content_h = h() * 2 / 3;
    auto content_l = new tree_view_t(supervisor, 0, 0, content_w, content_h);
    content_l->box(FL_ENGRAVED_BOX);

    auto content_r = new Fl_Box(content_w, 0, content_w, content_h, "content r");
    content_r->box(FL_ENGRAVED_BOX);

    log_panel = new log_panel_t(supervisor, 0, 0, w(), h() / 3);
    log_panel->position(0, h() / 3 * 2);
    log_panel->box(FL_FLAT_BOX);

    container->end();

    end();

    resizable(this);
}

main_window_t::~main_window_t() { delete log_panel; }
