// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "folder_info.h"
#include "folder.h"
#include "structs.pb.h"
#include "../db/prefix.h"
#include "misc/error_code.h"
#include <spdlog.h>

#ifdef uuid_t
#undef uuid_t
#endif

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::folder_info);

outcome::result<folder_info_ptr_t> folder_info_t::create(std::string_view key, const db::FolderInfo &data,
                                                         const device_ptr_t &device_,
                                                         const folder_ptr_t &folder_) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_folder_info_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_folder_info_prefix);
    }

    auto ptr = folder_info_ptr_t();
    ptr = new folder_info_t(key, device_, folder_);

    ptr->assign_fields(data);

    return outcome::success(std::move(ptr));
}

outcome::result<folder_info_ptr_t> folder_info_t::create(const uuid_t &uuid, const db::FolderInfo &data,
                                                         const device_ptr_t &device_,
                                                         const folder_ptr_t &folder_) noexcept {
    auto ptr = folder_info_ptr_t();
    ptr = new folder_info_t(uuid, device_, folder_);

    ptr->assign_fields(data);

    return outcome::success(std::move(ptr));
}

folder_info_t::folder_info_t(std::string_view key_, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept
    : device{device_.get()}, folder{folder_.get()} {
    assert(key_.substr(1, device_id_t::digest_length) == device->get_key().substr(1));
    assert(key_.substr(device_id_t::digest_length + 1, uuid_length) == folder->get_key().substr(1));
    std::copy(key_.begin(), key_.end(), key);
}

folder_info_t::folder_info_t(const uuid_t &uuid, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept
    : device{device_.get()}, folder{folder_.get()} {
    auto device_key = device->get_key().substr(1);
    auto folder_key = folder->get_key().substr(1);
    key[0] = prefix;
    std::copy(device_key.begin(), device_key.end(), key + 1);
    std::copy(folder_key.begin(), folder_key.end(), key + 1 + device_key.size());
    std::copy(uuid.begin(), uuid.end(), key + 1 + device_key.size() + folder_key.size());
}

folder_info_t::~folder_info_t() {}

void folder_info_t::assign_fields(const db::FolderInfo &fi) noexcept {
    index = fi.index_id();
    remote_max_sequence = max_sequence = fi.max_sequence();
}

std::string_view folder_info_t::get_key() noexcept { return std::string_view(key, data_length); }

std::string_view folder_info_t::get_uuid() noexcept {
    return std::string_view(key + 1 + device_id_t::digest_length + uuid_length, uuid_length);
}

bool folder_info_t::operator==(const folder_info_t &other) const noexcept {
    auto r = std::mismatch(key, key + data_length, other.key);
    return r.first == key + data_length;
}

void folder_info_t::add(const file_info_ptr_t &file_info) noexcept { file_infos.put(file_info); }

void folder_info_t::set_max_sequence(int64_t value) noexcept {
    assert(max_sequence < value);
    remote_max_sequence = max_sequence = value;
}

std::string folder_info_t::serialize() noexcept {
    db::FolderInfo r;
    r.set_index_id(index);
    r.set_max_sequence(max_sequence);
    return r.SerializeAsString();
}

bool folder_info_t::is_actual() noexcept { return max_sequence == remote_max_sequence; }

folder_info_ptr_t folder_infos_map_t::by_device(const device_ptr_t &device) const noexcept {
    return get<1>(device->device_id().get_sha256());
}

folder_info_ptr_t folder_infos_map_t::by_device_id(std::string_view device_id) const noexcept {
    return get<1>(device_id);
}

template <> std::string_view get_index<0>(const folder_info_ptr_t &item) noexcept { return item->get_uuid(); }
template <> std::string_view get_index<1>(const folder_info_ptr_t &item) noexcept {
    return item->get_device()->device_id().get_sha256();
}

} // namespace syncspirit::model
