#pragma once

#include <FL/Fl_Double_Window.H>
#include "application.h"

namespace syncspirit::fltk {

struct main_window_t : Fl_Double_Window {
    using parent_t = Fl_Double_Window;

    main_window_t(application_t &application_);
    ~main_window_t();

  private:
    application_t &application;
    Fl_Widget *log_panel;
};

} // namespace syncspirit::fltk
