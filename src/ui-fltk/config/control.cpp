#include "control.h"
#include "table.h"

#include <FL/Fl_Button.H>

namespace syncspirit::fltk::config {

static constexpr int PADDING = 5;

control_t::control_t(tree_item_t &tree_item_, int x, int y, int w, int h)
    : Fl_Group(x, y, w, h, "global app settings"), tree_item{tree_item_} {

    auto bottom_h = 40;
    auto table = new table_t({}, x + PADDING, y + PADDING, w - PADDING * 2, h - (bottom_h + PADDING * 3));

    auto bottom_group = new Fl_Group(x + PADDING, table->y() + table->h() + PADDING, w - PADDING * 2, bottom_h);
    bottom_group->begin();
    auto common_y = bottom_group->y() + PADDING;
    auto common_w = bottom_group->w() / 2 - PADDING;
    auto common_h = bottom_group->h() - PADDING * 2;
    auto save = new Fl_Button(bottom_group->x(), common_y, common_w, common_h, "save");
    auto reset = new Fl_Button(save->x() + common_w + PADDING, common_y, common_w, common_h, "defaults");
    bottom_group->end();
    // bottom_group->position(table->x(), table->h());

    resizable(table);
    box(FL_FLAT_BOX);
}

} // namespace syncspirit::fltk::config
