// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/file_info.h"
#include "entity.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct SYNCSPIRIT_API file_entity_t : entity_t {
    file_entity_t(model::file_info_t &sample_file, std::string_view own_name);

    void set_parent(entity_t *entry);
};

} // namespace syncspirit::presentation
