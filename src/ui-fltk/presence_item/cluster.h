// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file.h"

namespace syncspirit::fltk::tree_item {

struct cluster_t final : file_t {
    using parent_t = file_t;

    cluster_t(presentation::presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree);
};

} // namespace syncspirit::fltk::tree_item
