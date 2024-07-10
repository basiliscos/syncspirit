#include "base.h"

#include <FL/Fl_Check_Button.H>

namespace syncspirit::fltk::table_widget {

struct checkbox_t : base_t {
    using parent_t = base_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    Fl_Check_Button *input = nullptr;
};

} // namespace syncspirit::fltk::table_widget
