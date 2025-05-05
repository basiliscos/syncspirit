// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "missing_item_presence.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

missing_item_presence_t::missing_item_presence_t(presence_item_t *host_, presentation::presence_t &presence_)
    : parent_t(presence_, host_->supervisor, host_->tree(), false), host{host_} {
    using F = presentation::presence_t::features_t;
    assert(presence_.get_features() & F::missing);
    update_label();
    if (presence_.get_entity()->get_children().size()) {
        populate_dummy_child();
    }
}

auto missing_item_presence_t::get_device() const -> const model::device_t * {
    return host->get_presence().get_device();
}
