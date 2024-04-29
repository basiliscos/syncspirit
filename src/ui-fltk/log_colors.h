#pragma once

#include <FL/Enumerations.H>
#include <array>

namespace syncspirit::fltk {

using color_array_t = std::array<Fl_Color, 12>;

extern color_array_t log_colors;

} // namespace syncspirit::fltk
