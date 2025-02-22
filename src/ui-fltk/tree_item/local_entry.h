// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "file_entry.h"

namespace syncspirit::fltk::tree_item {

struct local_entry_t : file_entry_t {
    using parent_t = file_entry_t;
    using parent_t::parent_t;

    entry_t *create_child(augmentation_entry_t &entry) override;
};

}; // namespace syncspirit::fltk::tree_item
