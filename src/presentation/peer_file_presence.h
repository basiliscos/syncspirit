// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "cluster_file_presence.h"

namespace syncspirit::presentation {

struct SYNCSPIRIT_API peer_file_presence_t : cluster_file_presence_t {
    peer_file_presence_t(file_entity_t &entity, model::file_info_t &file_info) noexcept;
};

} // namespace syncspirit::presentation
