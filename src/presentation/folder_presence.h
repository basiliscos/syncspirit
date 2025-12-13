// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presence.h"
#include "model/folder_info.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct folder_entity_t;

struct SYNCSPIRIT_API folder_presence_t : presence_t {
    folder_presence_t(folder_entity_t &entity, model::folder_info_t &folder_info) noexcept;

    model::folder_info_t &get_folder_info() noexcept;
    const model::folder_info_t &get_folder_info() const noexcept;

  protected:
    model::folder_info_t &folder_info;
};

} // namespace syncspirit::presentation
