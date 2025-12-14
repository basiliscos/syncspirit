// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "remote_view.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::model::details;

template <typename T> inline static std::size_t get_hash(const T &key) noexcept {
    auto h1 = std::hash<decltype(key.folder_id)>{}(key.folder_id);
    auto h2 = std::hash<decltype(key.device_id)>{}(key.device_id);
    return h1 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2);
}

std::size_t remote_view_key_hash_t::operator()(const remote_view_key_t &key) const noexcept { return get_hash(key); }

std::size_t remote_view_key_hash_t::operator()(const transient_view_key_t &key) const noexcept { return get_hash(key); }

void remote_view_map_t::push(utils::bytes_view_t device_id, std::string_view folder_id, std::uint64_t index_id,
                             std::int64_t max_sequence) noexcept {
    auto f = std::string(folder_id);
    auto d = utils::bytes_t(device_id);
    auto key = details::remote_view_key_t(std::move(f), std::move(d));
    (*this)[key] = remote_view_t{index_id, max_sequence};
}

const remote_view_t *remote_view_map_t::get(utils::bytes_view_t device_id, std::string_view folder_id) const noexcept {
    auto key = details::transient_view_key_t{folder_id, device_id};
    auto it = find(key);
    if (it != end()) {
        return &it->second;
    }
    return {};
}
