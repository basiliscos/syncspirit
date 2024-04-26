#pragma once

#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <vector>
#include <memory>

#include "utils/log.h"

namespace syncspirit::fltk {

struct log_panel_t : Fl_Text_Display {
    using parent_t = Fl_Text_Display;

    struct log_record_t;
    using log_record_ptr_t = std::unique_ptr<log_record_t>;
    using records_t = std::vector<log_record_ptr_t>;
    using sink_ptr_t = spdlog::sink_ptr;

    log_panel_t(utils::dist_sink_t dist_sink, int x, int y, int w, int h);
    ~log_panel_t();

    void append(log_record_ptr_t record);

  private:
    records_t records;
    sink_ptr_t bridge_sink;
    utils::dist_sink_t dist_sink;
    Fl_Text_Buffer text_buffer;
    Fl_Text_Buffer style_buffer;
};

} // namespace syncspirit::fltk
