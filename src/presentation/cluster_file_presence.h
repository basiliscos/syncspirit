// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file_presence.h"
#include "syncspirit-export.h"
#include "model/file_info.h"

namespace syncspirit::presentation {

struct file_entity_t;

struct SYNCSPIRIT_API cluster_file_presence_t : file_presence_t {
    cluster_file_presence_t(file_entity_t &entity, model::file_info_t &file_info) noexcept;

    model::file_info_t &get_file_info() noexcept;

  protected:
    model::file_info_t &file_info;
};

} // namespace syncspirit::presentation
