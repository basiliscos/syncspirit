// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "cluster_file_presence.h"

namespace syncspirit::presentation {

struct SYNCSPIRIT_API local_file_presence_t : cluster_file_presence_t {
    local_file_presence_t(file_entity_t &entity, model::file_info_t &file_info, const model::folder_info_t &folder_info) noexcept;
};

} // namespace syncspirit::presentation
