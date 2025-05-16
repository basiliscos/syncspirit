// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file.h"

namespace syncspirit::fltk::tree_item {

struct missing_t final : file_t {
    using parent_t = file_t;

    missing_t(presence_item_t *host, presentation::presence_t &presence);
    ~missing_t();

    const model::device_t *get_device() const override;
    void on_delete() noexcept override;

    presence_item_t *host;
};

} // namespace syncspirit::fltk::tree_item
