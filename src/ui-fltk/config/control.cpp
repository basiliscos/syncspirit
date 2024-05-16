#include "control.h"

#include <FL/Fl_Button.H>

namespace syncspirit::fltk::config {

static constexpr int PADDING = 10;

control_t::control_t(tree_item_t &tree_item_, int x, int y, int w, int h)
    : Fl_Group(x, y, w, h, "global app settings"), tree_item{tree_item_} {

    auto bottom_group = new Fl_Group(x, y, w, h);
    bottom_group->begin();
    auto common_y = y + PADDING;
    auto common_w = (w / 2) - PADDING * 3;
    auto common_h = h - PADDING * 2;
    auto save = new Fl_Button(x + PADDING, common_y, common_w, common_h, "save");
    auto reset = new Fl_Button(save->x() + common_w + PADDING, common_y, common_w, common_h, "defaults");
    bottom_group->end();

    // resizable(bottom_group);
    box(FL_FLAT_BOX);
}

} // namespace syncspirit::fltk::config
