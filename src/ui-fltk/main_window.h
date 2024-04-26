#pragma once

#include <FL/Fl_Double_Window.H>
#include "utils/log.h"

namespace syncspirit::fltk {

struct main_window_t : Fl_Double_Window {
    using parent_t = Fl_Double_Window;

    main_window_t(utils::dist_sink_t dist_sink);
    ~main_window_t();

  private:
    Fl_Widget *log_panel;
};

} // namespace syncspirit::fltk
