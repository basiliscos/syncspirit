// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "pending_folder.h"
#include "db/prefix.h"
#include "misc/error_code.h"
#include "proto/proto-structs.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(syncspirit::db::prefix::pending_folder);

outcome::result<pending_folder_ptr_t> pending_folder_t::create(utils::bytes_view_t key,
                                                               const db::PendingFolder &data) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_pending_folder_length);
    }

    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_folder_prefix);
    }

    auto sha256 = key.subspan(uuid_length + 1);
    auto device = device_id_t::from_sha256(sha256);
    if (!device) {
        return make_error_code(error_code_t::malformed_deviceid);
    }

    auto ptr = pending_folder_ptr_t();
    ptr = new pending_folder_t(key, device.value());
    ptr->assign_fields(data);
    return outcome::success(std::move(ptr));
}

outcome::result<pending_folder_ptr_t> pending_folder_t::create(const bu::uuid &uuid, const db::PendingFolder &data,
                                                               const device_id_t &device_) noexcept {
    auto ptr = pending_folder_ptr_t();
    ptr = new pending_folder_t(uuid, device_);
    ptr->assign_fields(data);
    return outcome::success(std::move(ptr));
}

pending_folder_t::pending_folder_t(const bu::uuid &uuid, const device_id_t &device_) noexcept : device{device_} {
    key[0] = prefix;
    std::copy(uuid.begin(), uuid.end(), key + 1);
    auto sha256 = device.get_sha256();
    std::copy(sha256.begin(), sha256.end(), key + 1 + uuid_length);
}

pending_folder_t::pending_folder_t(utils::bytes_view_t key_, const device_id_t &device_) noexcept : device{device_} {
    std::copy(key_.begin(), key_.end(), key);
}

void pending_folder_t::assign_fields(const db::PendingFolder &data) noexcept {
    folder_data_t::assign_fields(data.folder());
    auto fi = data.folder_info();
    index = fi.index_id();
    max_sequence = fi.max_sequence();
    id = data.folder().id();
}

void pending_folder_t::serialize(db::PendingFolder &data) const noexcept {
    auto folder = data.mutable_folder();
    folder_data_t::serialize(folder);
    auto fi = data.mutable_folder_info();
    fi.index_id(index);
    fi.max_sequence(max_sequence);
}

utils::bytes_t pending_folder_t::serialize() const noexcept {
    db::PendingFolder r;
    serialize(r);
    return r.encode();
}

template <> SYNCSPIRIT_API std::string_view get_index<0>(const pending_folder_ptr_t &item) noexcept {
    auto key = item->get_key();
    auto ptr = (const char*)key.data();
    return {ptr, key.size()};
}

template <> SYNCSPIRIT_API std::string_view get_index<1>(const pending_folder_ptr_t &item) noexcept {
    return item->get_id();
}

pending_folder_ptr_t pending_folder_map_t::by_key(utils::bytes_view_t key) const noexcept {
    auto ptr = (const char*)key.data();
    return get<0>(std::string_view(ptr, key.size()));
}

pending_folder_ptr_t pending_folder_map_t::by_id(std::string_view id) const noexcept { return get<1>(id); }

} // namespace syncspirit::model
