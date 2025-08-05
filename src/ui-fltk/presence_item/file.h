// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presence_item.h"

namespace syncspirit::fltk::tree_item {

struct file_t : presence_item_t {
    using parent_t = presence_item_t;
    using parent_t::parent_t;

    bool on_select() override;
};

} // namespace syncspirit::fltk::tree_item
