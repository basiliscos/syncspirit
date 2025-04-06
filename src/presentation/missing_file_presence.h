// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file_presence.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct file_entity_t;

struct SYNCSPIRIT_API missing_file_presence_t : file_presence_t {
    missing_file_presence_t(file_entity_t &entity) noexcept;
    ~missing_file_presence_t();
};

} // namespace syncspirit::presentation
