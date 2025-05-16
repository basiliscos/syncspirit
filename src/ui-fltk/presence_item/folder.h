// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "presence_item.h"
#include "presentation/folder_entity.h"

namespace syncspirit::fltk::presence_item {

struct folder_t final : presence_item_t {
    using parent_t = presence_item_t;

    folder_t(presentation::folder_presence_t &folder, app_supervisor_t &supervisor, Fl_Tree *tree);
    ~folder_t();
    bool on_select() override;
};

} // namespace syncspirit::fltk::presence_item
