#include "log_panel.h"

#include <FL/Fl_Button.H>
#include "log_table.h"

using namespace syncspirit::fltk;

void copy_callback(Fl_Widget*, void* v) {
    printf("cb\n");
    // Slider* slider = (Slider*)v;
    // slider->value(intinput->ivalue());
}


log_panel_t::log_panel_t(application_t &application, int x, int y, int w, int h): parent_t{x, y, w, h}
{
    int padding = 0;
    // auto log_table = new log_table_t(application, padding, padding, w - padding * 2, h - padding * 4);
    auto log_table = new log_table_t(application, padding, padding, w, h - padding * 4);
    // auto addDest = new Fl_Button(padding, log_table->h() + padding * 2, 40, 20, "add");
    // begin();
    // auto addDest = new Fl_Button(padding, 0, 40, 10, "add");
    // addDest->callback();
    // add(addDest);
    end();

    // resizable(this);
}
