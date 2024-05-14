#pragma once

#include <FL/Enumerations.H>
#include <array>

namespace syncspirit::fltk {

using color_array_t = std::array<Fl_Color, 12>;

extern color_array_t log_colors;
extern Fl_Color table_selection_color;

} // namespace syncspirit::fltk
