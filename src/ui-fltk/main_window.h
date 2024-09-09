#pragma once

#include <FL/Fl_Double_Window.H>
#include "app_supervisor.h"

namespace syncspirit::fltk {

struct main_window_t : Fl_Double_Window {
    using parent_t = Fl_Double_Window;

    main_window_t(app_supervisor_t &supervisor, int w, int h);
    ~main_window_t();

    void on_shutdown();

  private:
    app_supervisor_t &supervisor;
    Fl_Group *content_left;
    Fl_Widget *log_panel;
};

} // namespace syncspirit::fltk
