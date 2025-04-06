// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "entity.h"
#include "orphans.h"
#include "model/file_info.h"

namespace syncspirit::presentation {

struct folder_presence_t;

struct SYNCSPIRIT_API folder_entity_t : entity_t {
    folder_entity_t(model::folder_ptr_t folder) noexcept;
    model::folder_t &get_folder() noexcept;
    void on_insert(model::folder_info_t &folder_info) noexcept;
    void on_insert(model::file_info_t &file_info) noexcept;

  private:
    model::folder_t &folder;
    orphans_t orphans;
};

using folder_entity_ptr_t = model::intrusive_ptr_t<folder_entity_t>;

} // namespace syncspirit::presentation
