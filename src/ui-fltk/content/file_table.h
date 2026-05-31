// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include "static_table.h"
#include "presence_item.h"

namespace syncspirit::fltk::content {

struct file_table_t : static_table_t {
    using parent_t = static_table_t;

    file_table_t(presence_item_t &container_, int x, int y, int w, int h);

    void refresh() override;

    void on_scan();

    presence_item_t &container;

  private:
    static_string_provider_ptr_t name_cell;
    static_string_provider_ptr_t device_cell;
    static_string_provider_ptr_t modified_cell;
    static_string_provider_ptr_t sequence_cell;
    static_string_provider_ptr_t size_cell;
    static_string_provider_ptr_t block_size_cell;
    static_string_provider_ptr_t blocks_cell;
    static_string_provider_ptr_t permissions_cell;
    static_string_provider_ptr_t modified_s_cell;
    static_string_provider_ptr_t modified_ns_cell;
    static_string_provider_ptr_t symlink_target_cell;
    static_string_provider_ptr_t entries_cell;
    static_string_provider_ptr_t entries_size_cell;
    static_string_provider_ptr_t local_entries_cell;
    static_string_provider_ptr_t notice_cell;
    size_t displayed_versions;
};

} // namespace syncspirit::fltk::content
