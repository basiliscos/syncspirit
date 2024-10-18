// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "folder_info.h"
#include "folder.h"
#include "cluster.h"
#include "structs.pb.h"
#include "../db/prefix.h"
#include "misc/error_code.h"
#include <spdlog/spdlog.h>
#include <algorithm>

#ifdef uuid_t
#undef uuid_t
#endif

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::folder_info);

folder_info_t::decomposed_key_t::decomposed_key_t(std::string_view reduced_key, std::string_view folder_uuid,
                                                  std::string_view folder_info_id_)
    : folder_info_id{folder_info_id_} {
    assert(reduced_key.size() == device_id_t::digest_length);
    assert(folder_uuid.size() == uuid_length);
    device_key_raw[0] = (char)(db::prefix::device);
    folder_key_raw[0] = (char)(db::prefix::folder);
    std::copy(begin(reduced_key), end(reduced_key), device_key_raw + 1);
    std::copy(begin(folder_uuid), end(folder_uuid), folder_key_raw + 1);
}

auto folder_info_t::decompose_key(std::string_view key) -> decomposed_key_t {
    assert(key.size() == folder_info_t::data_length);
    auto device_key = key.substr(1, device_id_t::digest_length);
    auto folder_id = key.substr(1 + device_key.size(), uuid_length);
    auto folder_info_id = key.substr(1 + device_key.size() + folder_id.size(), uuid_length);
    return decomposed_key_t(device_key, folder_id, folder_info_id);
}

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

void folder_info_t::assign_fields(const db::FolderInfo &fi) noexcept {
    index = fi.index_id();
    max_sequence = fi.max_sequence();
    max_sequence == 0;
}

std::string_view folder_info_t::get_key() const noexcept { return std::string_view(key, data_length); }

std::string_view folder_info_t::get_uuid() const noexcept {
    return std::string_view(key + 1 + device_id_t::digest_length + uuid_length, uuid_length);
}

bool folder_info_t::operator==(const folder_info_t &other) const noexcept {
    auto r = std::mismatch(key, key + data_length, other.key);
    return r.first == key + data_length;
}

void folder_info_t::add(const file_info_ptr_t &file_info, bool inc_max_sequence) noexcept {
    file_infos.put(file_info);
    auto seq = file_info->get_sequence();
    if (inc_max_sequence && seq > max_sequence) {
        max_sequence = seq;
    } else {
        assert(seq <= max_sequence);
    }
}

void folder_info_t::serialize(db::FolderInfo &storage) const noexcept {
    storage.set_index_id(index);
    storage.set_max_sequence(max_sequence);
}

std::string folder_info_t::serialize() const noexcept {
    db::FolderInfo r;
    serialize(r);
    return r.SerializeAsString();
}

void folder_info_t::set_max_sequence(std::int64_t value) noexcept { max_sequence = value; }

void folder_info_t::set_index(std::uint64_t value) noexcept {
    if (value != this->index) {
        index = value;
        file_infos.clear();
    }
}

std::optional<proto::Index> folder_info_t::generate() noexcept {
    auto &folder_infos = folder->get_folder_infos();
    auto &remote_folders = device->get_remote_folder_infos();
    auto remote_folder = remote_folders.by_folder(*folder);
    if (remote_folder) {
        auto &rf = *remote_folder;
        auto &local_device = *folder->get_cluster()->get_device();
        auto local_folder = folder_infos.by_device(local_device);
        bool need_initiate = (rf.get_index() != local_folder->index) || (!rf.get_max_sequence());
        if (need_initiate) {
            proto::Index r;
            r.set_folder(std::string(folder->get_id()));
            return r;
        }
    }
    return {};
}

folder_info_ptr_t folder_infos_map_t::by_device(const device_t &device) const noexcept {
    return get<1>(device.device_id().get_sha256());
}

folder_info_ptr_t folder_infos_map_t::by_device_id(std::string_view device_id) const noexcept {
    return get<1>(device_id);
}

folder_info_ptr_t folder_infos_map_t::by_device_key(std::string_view device_key) const noexcept {
    return get<2>(device_key);
}

template <> SYNCSPIRIT_API std::string_view get_index<0>(const folder_info_ptr_t &item) noexcept {
    return item->get_uuid();
}

template <> SYNCSPIRIT_API std::string_view get_index<1>(const folder_info_ptr_t &item) noexcept {
    return item->get_device()->device_id().get_sha256();
}

template <> SYNCSPIRIT_API std::string_view get_index<2>(const folder_info_ptr_t &item) noexcept {
    return item->get_device()->get_key();
}

} // namespace syncspirit::model
