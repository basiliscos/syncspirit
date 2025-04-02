// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presence.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct file_entity_t;

struct SYNCSPIRIT_API file_presence_t : presence_t {
    // clang-format off
    enum features_t: std::uint32_t {
        missing = 0b00000001,
        cluster = 0b00000010,
        peer    = 0b00000100,
        local   = 0b00001000,
        deleted = 0b00010000,
        ignored = 0b00100000,
    };
    // clang-format ON

    using presence_t::presence_t;

    std::uint32_t get_presence_feautres();

  protected:
    std::uint32_t features = 0;
};

} // namespace syncspirit::presentation
