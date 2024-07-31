#include "path.h"

#include "FL/Fl_Button.H"
#include <FL/Fl_Native_File_Chooser.H>

static constexpr int padding = 2;

using namespace syncspirit::fltk::table_widget;

path_t::path_t(Fl_Widget &container_, std::string title_) : parent_t{container_}, title{title_} {}

Fl_Widget *path_t::create_widget(int x, int y, int w, int h) {
    auto group = new Fl_Group(x, y, w, h);
    group->begin();
    group->box(FL_FLAT_BOX);

    auto button_w = 25;
    auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
    input = new Fl_Input(x + padding, yy, ww - (button_w + padding * 2), hh);

    auto xx = input->x();
    auto button = new Fl_Button(xx + input->w() + padding, yy, button_w, hh, "...");
    button->callback([](Fl_Widget *, void *data) { reinterpret_cast<path_t *>(data)->on_click(); }, this);

    group->resizable(input);
    group->end();
    widget = group;
    reset();
    return widget;
}

void path_t::on_click() {
    using T = Fl_Native_File_Chooser::Type;
    auto type = T::BROWSE_DIRECTORY;

    Fl_Native_File_Chooser file_chooser;
    file_chooser.title(title.data());
    file_chooser.type(type);
    auto r = file_chooser.show();
    if (r != 0) {
        return;
    }

    input->value(file_chooser.filename());
}
