// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "entry.h"

namespace syncspirit::fltk::tree_item {

struct file_entry_t : entry_t {
    using parent_t = entry_t;
    using parent_t::parent_t;
    bool on_select() override;
};

}; // namespace syncspirit::fltk::tree_item
