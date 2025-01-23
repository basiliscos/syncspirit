// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "peer_entry.h"
#include "../content/remote_file_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

entry_t *peer_entry_t::create_child(augmentation_entry_t &entry) {
    return new peer_entry_t(supervisor, tree(), &entry, this);
}
