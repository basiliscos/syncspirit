#include "toolbar.h"

#include <FL/Fl_Toggle_Button.H>

using namespace syncspirit::fltk;

static constexpr int padding = 2;
static constexpr int button_h = 8;

toolbar_t::toolbar_t(app_supervisor_t &supervisor_, int x, int y, int w, int h)
    : parent_t(x, y, w, button_h + padding * 2), supervisor{supervisor_} {
    auto button_show_deleted = new Fl_Toggle_Button(x + padding, y + padding, 40 - padding * 2, button_h, "d");
    button_show_deleted->tooltip("show deleted");
    end();
}
