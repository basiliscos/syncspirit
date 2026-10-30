// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "folder_info.h"
#include "folder.h"
#include "proto/proto-helpers-db.h"
#include "db/prefix.h"
#include "misc/error_code.h"
#include "model/misc/folder_info_iterator.h"
#include "utils/format.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::folder_info);

folder_info_t::decomposed_key_t::decomposed_key_t(utils::bytes_view_t reduced_key, utils::bytes_view_t folder_uuid,
                                                  utils::bytes_view_t folder_info_id_)
    : folder_info_id{folder_info_id_} {
    assert(reduced_key.size() == device_id_t::digest_length);
    assert(folder_uuid.size() == uuid_length);
    device_key_raw[0] = (char)(db::prefix::device);
    folder_key_raw[0] = (char)(db::prefix::folder);
    std::copy(begin(reduced_key), end(reduced_key), device_key_raw + 1);
    std::copy(begin(folder_uuid), end(folder_uuid), folder_key_raw + 1);
}

auto folder_info_t::decompose_key(utils::bytes_view_t key) -> decomposed_key_t {
    assert(key.size() == folder_info_t::data_length);
    auto device_key = key.subspan(1, device_id_t::digest_length);
    auto folder_id = key.subspan(1 + device_key.size(), uuid_length);
    auto folder_info_id = key.subspan(1 + device_key.size() + folder_id.size(), uuid_length);
    return decomposed_key_t(device_key, folder_id, folder_info_id);
}

bool folder_info_t::path_eq_t::operator()(const utils::path_base_t *lhs, std::string_view rhs) const {
    if (lhs) {
        return lhs->get_full_name() == rhs;
    }
    return false;
}

bool folder_info_t::path_eq_t::operator()(std::string_view lhs, const utils::path_base_t *rhs) const {
    if (rhs) {
        return lhs == rhs->get_full_name();
    }
    return false;
}

bool folder_info_t::path_eq_t::operator()(const utils::path_base_t *lhs, const utils::path_base_t *rhs) const {
    if (lhs == rhs) {
        return true;
    } else if (lhs && rhs) {
        return *lhs == *rhs;
    }
    return false;
}

outcome::result<folder_info_ptr_t> folder_info_t::create(utils::bytes_view_t key, const db::FolderInfo &data,
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

outcome::result<folder_info_ptr_t> folder_info_t::create(const bu::uuid &uuid, const db::FolderInfo &data,
                                                         const device_ptr_t &device_,
                                                         const folder_ptr_t &folder_) noexcept {
    auto ptr = folder_info_ptr_t();
    ptr = new folder_info_t(uuid, device_, folder_);

    ptr->assign_fields(data);

    return outcome::success(std::move(ptr));
}

folder_info_t::folder_info_t(utils::bytes_view_t key_, const device_ptr_t &device_,
                             const folder_ptr_t &folder_) noexcept
    : presence_t(folder_.get(), device_.get()) {
    assert(key_.subspan(1, device_id_t::digest_length) == device->get_key().subspan(1));
    assert(key_.subspan(device_id_t::digest_length + 1, uuid_length) == get_folder()->get_key().subspan(1));
    std::copy(key_.begin(), key_.end(), key);
}

folder_info_t::folder_info_t(const bu::uuid &uuid, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept
    : presence_t(folder_.get(), device_.get()) {
    auto device_key = device->get_key().subspan(1);
    auto folder_key = get_folder()->get_key().subspan(1);
    key[0] = prefix;
    std::copy(device_key.begin(), device_key.end(), key + 1);
    std::copy(folder_key.begin(), folder_key.end(), key + 1 + device_key.size());
    std::copy(uuid.begin(), uuid.end(), key + 1 + device_key.size() + folder_key.size());
}

folder_t *folder_info_t::get_folder() const noexcept { return static_cast<folder_t *>(entity); }

void folder_info_t::assign_fields(const db::FolderInfo &fi) noexcept {
    features |= features_t::folder;
    index = db::get_index_id(fi);
    max_sequence = db::get_max_sequence(fi);
    introducer_device_key = db::get_introducer_device_key(fi);
}

utils::bytes_view_t folder_info_t::get_key() const noexcept { return utils::bytes_view_t(key, data_length); }

utils::bytes_view_t folder_info_t::get_uuid() const noexcept {
    return utils::bytes_view_t(key + 1 + device_id_t::digest_length + uuid_length, uuid_length);
}

bool folder_info_t::operator==(const folder_info_t &other) const noexcept {
    auto r = std::mismatch(key, key + data_length, other.key);
    return r.first == key + data_length;
}

void folder_info_t::add_file(file_info_t *file_info) noexcept {
    auto path = file_info->get_name().get();
    auto ok = file_cache.insert({path, file_info}).second;
    assert(ok);
    (void)ok;
    auto parent_name = path->get_parent_name();
    auto parent = parent_name.empty() ? this : [&]() -> presence_t * {
        auto it = file_cache.find(parent_name);
        return it == file_cache.end() ? nullptr : it->second;
    }();
    if (!parent) {
        spdlog::critical("missing parent presence for '{}' in folder '{}' for device {}", *path, get_folder()->get_id(),
                         device->device_id().get_short());
        assert(parent);
    }
    auto filename = path->get_filename();
    auto entity = parent->get_entity()->get_or_create_child(filename);
    parent->add_child(file_info, filename);

    update_max_sequnece(*file_info);
}

void folder_info_t::update_max_sequnece(const file_info_t &file_info) noexcept {
    if (auto seq = file_info.get_sequence(); seq > max_sequence) {
        max_sequence = seq;
    }
}

void folder_info_t::serialize(db::FolderInfo &storage) const noexcept {
    db::set_index_id(storage, index);
    db::set_max_sequence(storage, max_sequence);
    db::set_introducer_device_key(storage, introducer_device_key);
}

utils::bytes_t folder_info_t::serialize() const noexcept {
    db::FolderInfo r;
    serialize(r);
    return db::encode(r);
}

void folder_info_t::set_index(std::uint64_t value) noexcept {
    if (value != this->index) {
        index = value;
        max_sequence = 0;
        clear();
    }
}

bool folder_info_t::is_introduced_by(const model::device_id_t &device) const noexcept {
    return introducer_device_key == device.get_key();
}

utils::bytes_view_t folder_info_t::get_introducer_device_key() const noexcept { return introducer_device_key; }

file_info_t *folder_info_t::get_by_path(const utils::path_base_t &path) const noexcept {
    auto it = file_cache.find(&path);
    return it != file_cache.end() ? it->second : nullptr;
}

void folder_info_t::clear() noexcept {
    while (children.size()) {
        children.back()->forget_presence();
    }
}

void folder_info_t::forget_file(const utils::path_base_t &path) noexcept {
    auto it = file_cache.find(&path);
    assert(it != file_cache.end());
    file_cache.erase(it);
}

folder_info_iterator_ptr_t folder_info_t::iterate() noexcept {
    normalize();
    return new folder_info_iterator_t(this);
}

} // namespace syncspirit::model
