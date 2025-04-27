// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presence_item.h"

namespace syncspirit::fltk::tree_item {

struct missing_item_presence_t final : presence_item_t {
    using parent_t = presence_item_t;

    missing_item_presence_t(presence_item_t *host, presentation::presence_t &presence);

    void update_label() override;
    model::device_t *get_device() override;

    presence_item_t *host;
};

} // namespace syncspirit::fltk::tree_item
