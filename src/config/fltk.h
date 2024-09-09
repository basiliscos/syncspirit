// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include <spdlog/spdlog.h>

namespace syncspirit::config {

struct fltk_config_t {
    spdlog::level::level_enum level;
    bool display_deleted;
    int main_window_width;
    int main_window_height;
    double left_panel_share;
    double bottom_panel_share;
};

} // namespace syncspirit::config
