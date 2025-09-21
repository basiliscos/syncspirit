// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/file_info.h"
#include "model/folder_info.h"
#include "entity.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct file_presence_t;

struct SYNCSPIRIT_API file_entity_t : entity_t {
    file_entity_t(model::path_ptr_t path, const model::folder_infos_map_t &fi_map) noexcept;
    void set_parent(entity_t *entity) noexcept;
    auto on_insert(model::file_info_t &, const model::folder_info_t &) noexcept -> file_presence_t *;

  private:
    presence_ptr_t missing_file;
};

} // namespace syncspirit::presentation
