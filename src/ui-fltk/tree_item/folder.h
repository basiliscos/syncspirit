// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "local_entry.h"

namespace syncspirit::fltk::tree_item {

struct folder_t final : local_entry_t {
    using parent_t = local_entry_t;

    folder_t(model::folder_t &folder, app_supervisor_t &supervisor, Fl_Tree *tree);
    void update_label() override;
    bool on_select() override;
};

} // namespace syncspirit::fltk::tree_item
