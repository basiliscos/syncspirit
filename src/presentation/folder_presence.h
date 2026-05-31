// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "presence.h"
#include "model/folder_info.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct folder_entity_t;

struct presence_link_t {
    presence_t *parent;
    presence_t *child;
};

struct SYNCSPIRIT_API folder_presence_t : presence_t {
    folder_presence_t(folder_entity_t &entity, model::folder_info_t &folder_info) noexcept;

    model::folder_info_t &get_folder_info() noexcept;
    const model::folder_info_t &get_folder_info() const noexcept;
    presence_link_t get_link(std::string_view name, bool is_dir) const noexcept;

  protected:
    model::folder_info_t &folder_info;
};

} // namespace syncspirit::presentation
