// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include "utils/bytes.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

struct remote_view_t {
    std::uint64_t index_id;
    std::int64_t max_sequence;
};

namespace details {

struct remote_view_key_t {
    std::string folder_id;
    utils::bytes_t device_id;
};

struct transient_view_key_t {
    std::string_view folder_id;
    utils::bytes_view_t device_id;
};

struct SYNCSPIRIT_API remote_view_key_eq_t {
    using is_transparent = void;
    template <typename U, typename V> bool operator()(const U &lhs, const V &rhs) const {
        return lhs.folder_id == rhs.folder_id && lhs.device_id == rhs.device_id;
    }
};

struct SYNCSPIRIT_API remote_view_key_hash_t {
    using is_transparent = void;
    std::size_t operator()(const remote_view_key_t &key) const noexcept;
    std::size_t operator()(const transient_view_key_t &key) const noexcept;
};

using remote_view_map_base_t =
    std::unordered_map<remote_view_key_t, remote_view_t, remote_view_key_hash_t, remote_view_key_eq_t>;

} // namespace details

struct SYNCSPIRIT_API remote_view_map_t : details::remote_view_map_base_t {
    void push(utils::bytes_view_t device_id, std::string_view folder_id, std::uint64_t index_id,
              std::int64_t max_sequence) noexcept;
    const remote_view_t *get(utils::bytes_view_t device_id, std::string_view folder_id) const noexcept;
};

} // namespace syncspirit::model
