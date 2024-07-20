#include "choice.h"

static constexpr int padding = 2;

using namespace syncspirit::fltk::table_widget;

Fl_Widget *choice_t::create_widget(int x, int y, int w, int h) {
    auto group = new Fl_Group(x, y, w, h);
    group->begin();
    group->box(FL_FLAT_BOX);
    auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
    ww = std::min(150, ww);

    input = new Fl_Choice(x + padding, yy, ww, hh);
    group->end();
    group->resizable(nullptr);
    widget = group;

    reset();
    return widget;
}
