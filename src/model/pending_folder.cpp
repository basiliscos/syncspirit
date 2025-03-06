// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "pending_folder.h"
#include "db/prefix.h"
#include "misc/error_code.h"
#include "proto/proto-helpers.h"

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
    auto& f = db::get_folder(data);
    id = db::get_id(f);
    folder_data_t::assign_fields(f);
    auto& fi = db::get_folder_info(data);
    index = db::get_index_id(fi);
    max_sequence = db::get_max_sequence(fi);
}

void pending_folder_t::serialize(db::PendingFolder &data) const noexcept {
    auto& folder = db::get_folder(data);
    folder_data_t::serialize(folder);
    auto& fi = db::get_folder_info(data);
    db::set_index_id(fi, index);
    db::set_max_sequence(fi, max_sequence);
}

utils::bytes_t pending_folder_t::serialize() const noexcept {
    db::PendingFolder r;
    serialize(r);
    return db::encode(r);
}

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<0>(const pending_folder_ptr_t &item) noexcept {
    return item->get_key();
}

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<1>(const pending_folder_ptr_t &item) noexcept {
    auto id = item->get_id();
    auto ptr = (unsigned char*)id.data();
    return {ptr, id.size()};
}

pending_folder_ptr_t pending_folder_map_t::by_key(utils::bytes_view_t key) const noexcept {
    return get<0>(key);
}

pending_folder_ptr_t pending_folder_map_t::by_id(std::string_view id) const noexcept {
    auto ptr = (unsigned char*) id.data();
    auto view = utils::bytes_view_t(ptr, id.size());
    return get<1>(view);
}

} // namespace syncspirit::model
