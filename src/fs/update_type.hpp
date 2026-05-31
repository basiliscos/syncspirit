// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::fs {

using update_type_internal_t = std::uint32_t;
namespace update_type {
// clang-format off
static constexpr update_type_internal_t CREATED_1 = 0b00001;
static constexpr update_type_internal_t CREATED   = 0b00010;
static constexpr update_type_internal_t DELETED   = 0b00100;
static constexpr update_type_internal_t META      = 0b01000;
static constexpr update_type_internal_t CONTENT   = 0b10000;
// clang-format on
} // namespace update_type

enum class update_type_t : update_type_internal_t {
    created = update_type::CREATED,
    deleted = update_type::DELETED,
    meta = update_type::META,
    content = update_type::CONTENT,
};

} // namespace syncspirit::fs
