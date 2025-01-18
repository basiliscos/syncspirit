#pragma once

#include "app_supervisor.h"
#include <FL/Fl_Double_Window.H>
#include <string>

namespace syncspirit::fltk {

struct log_panel_t;
struct tree_view_t;

struct main_window_t : Fl_Double_Window {
    using parent_t = Fl_Double_Window;

    main_window_t(app_supervisor_t &supervisor, int w, int h);

    void on_shutdown();
    void set_splash_text(std::string text);
    void on_loading_done();

  private:
    app_supervisor_t &supervisor;
    Fl_Group *content_left;
    tree_view_t *tree;
    log_panel_t *log_panel;
};

} // namespace syncspirit::fltk
