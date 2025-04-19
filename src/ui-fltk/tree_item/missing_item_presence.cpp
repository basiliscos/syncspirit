// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "missing_item_presence.h"
#include "presentation/cluster_file_presence.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

missing_item_presence_t::missing_item_presence_t(presence_item_t *host_, presentation::presence_t &presence_)
    : parent_t(presence_, host_->supervisor, host_->tree()), host{host_} {
    using F = presentation::presence_t::features_t;
    assert(presence_.get_features() & F::missing);
    update_label();
    if (presence_.get_entity()->get_children().size()) {
        populate_dummy_child();
    }
}

void missing_item_presence_t::update_label() {
    auto &p = static_cast<presentation::cluster_file_presence_t &>(presence);
    auto color = get_color();
    auto name = p.get_entity()->get_path().get_own_name();
    labelfgcolor(color);
    label(name.data());
}
