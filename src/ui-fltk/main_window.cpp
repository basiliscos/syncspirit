#include "main_window.h"
#include "log_panel.h"

using namespace syncspirit::fltk;

main_window_t::main_window_t(utils::dist_sink_t dist_sink_) : parent_t(640, 480, "syncspirit-fltk") {

    int padding = 10;

    log_panel = new log_panel_t(std::move(dist_sink_), padding, padding, w() - padding * 2, h() - padding * 2);
    end();
}

main_window_t::~main_window_t() { delete log_panel; }
