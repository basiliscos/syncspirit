// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "folder_info.h"
#include "file_info.h"
#include "cluster.h"
#include "misc/error_code.h"
#include "misc/version_utils.h"
#include "db/prefix.h"
#include "fs/utils.h"
#include <algorithm>
#include <spdlog/spdlog.h>

#ifdef uuid_t
#undef uuid_t
#endif

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::file_info);

outcome::result<file_info_ptr_t> file_info_t::create(std::string_view key, const db::FileInfo &data,
                                                     const folder_info_ptr_t &folder_info_) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_file_info_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_file_info_prefix);
    }

    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(key, folder_info_);

    auto r = ptr->fields_update(data);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

auto file_info_t::create(const uuid_t &uuid_, const proto::FileInfo &info_,
                         const folder_info_ptr_t &folder_info_) noexcept -> outcome::result<file_info_ptr_t> {
    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(uuid_, folder_info_);

    auto r = ptr->fields_update(info_, info_.blocks_size());
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

file_info_t::file_info_t(std::string_view key_, const folder_info_ptr_t &folder_info_) noexcept
    : folder_info{folder_info_.get()} {
    assert(key_.substr(1, uuid_length) == folder_info->get_uuid());
    std::copy(key_.begin(), key_.end(), key);
}

static void fill(char *key, const uuid_t &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    key[0] = prefix;
    auto fi_key = folder_info_->get_uuid();
    std::copy(fi_key.begin(), fi_key.end(), key + 1);
    std::copy(uuid.begin(), uuid.end(), key + 1 + fi_key.size());
}

std::string file_info_t::create_key(const uuid_t &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    std::string key;
    key.resize(data_length);
    fill(key.data(), uuid, folder_info_);
    return key;
}

file_info_t::file_info_t(const uuid_t &uuid, const folder_info_ptr_t &folder_info_) noexcept
    : folder_info{folder_info_.get()} {
    fill(key, uuid, folder_info_);
}

file_info_t::~file_info_t() {
    for (auto &b : blocks) {
        if (!b) {
            continue;
        }
        auto indices = b->unlink(this);
        for (auto i : indices) {
            blocks[i].reset();
        }
    }
}

std::string_view file_info_t::get_name() const noexcept { return name; }

std::uint64_t file_info_t::get_block_offset(size_t block_index) const noexcept {
    assert(!blocks.empty());
    return block_size * block_index;
}

template <typename Source>
outcome::result<void> file_info_t::fields_update(const Source &s, size_t block_count) noexcept {
    name = s.name();
    sequence = s.sequence();
    type = s.type();
    set_size(s.size());
    permissions = s.permissions();
    modified_s = s.modified_s();
    modified_ns = s.modified_ns();
    modified_by = s.modified_by();
    if (s.deleted()) {
        flags |= flags_t::f_deleted;
    }
    if (s.invalid()) {
        flags |= flags_t::f_invalid;
    }
    if (s.no_permissions()) {
        flags |= flags_t::f_no_permissions;
    }
    version = s.version();
    block_size = s.block_size();
    symlink_target = s.symlink_target();
    version = s.version();
    full_name = fmt::format("{}/{}", folder_info->get_folder()->get_label(), get_name());
    if constexpr (std::is_same_v<Source, db::FileInfo>) {
        source_device = s.source_device();
        source_version = s.source_version();
    }
    return reserve_blocks(block_count);
}

auto file_info_t::fields_update(const db::FileInfo &source) noexcept -> outcome::result<void> {
    return fields_update<db::FileInfo>(source, source.blocks_size());
}

std::string_view file_info_t::get_uuid() const noexcept { return std::string_view(key + 1 + uuid_length, uuid_length); }

void file_info_t::set_sequence(std::int64_t value) noexcept { sequence = value; }

template <typename T> T file_info_t::as() const noexcept {
    T r;
    auto name = get_name();
    r.set_name(name.data(), name.size());
    r.set_sequence(sequence);
    r.set_type(type);
    r.set_size(size);
    r.set_permissions(permissions);
    r.set_modified_s(modified_s);
    r.set_modified_ns(modified_ns);
    r.set_modified_by(modified_by);
    r.set_deleted(flags & f_deleted);
    r.set_invalid(flags & f_invalid);
    r.set_no_permissions(flags & f_no_permissions);
    *r.mutable_version() = version;
    r.set_block_size(block_size);
    r.set_symlink_target(symlink_target);
    return r;
}

db::FileInfo file_info_t::as_db(bool include_blocks) const noexcept {
    auto r = as<db::FileInfo>();

    if (include_blocks) {
        for (auto &block : blocks) {
            if (!block) {
                continue;
            }
            auto db_key = block->get_hash();
            r.mutable_blocks()->Add(std::string(db_key));
        }
    }
    r.set_source_device(source_device);
    (*r.mutable_source_version()) = source_version;
    return r;
}

proto::FileInfo file_info_t::as_proto(bool include_blocks) const noexcept {
    assert(!include_blocks && "TODO");
    return as<proto::FileInfo>();
}

outcome::result<void> file_info_t::reserve_blocks(size_t block_count) noexcept {
    size_t count = 0;
    if (!block_count && !(flags & f_deleted) && !(flags & f_invalid)) {
        if ((size < block_size) && (size >= fs::block_sizes[0])) {
            return make_error_code(error_code_t::invalid_block_size);
        }
        if (size) {
            if (!block_size) {
                return make_error_code(error_code_t::invalid_block_size);
            }
            count = size / block_size;
            if (block_size * count != size) {
                ++count;
            }
        }
    } else {
        count = block_count;
    }
    remove_blocks();
    blocks.resize(count);
    marks.resize(count);
    missing_blocks = !(flags & f_deleted) && !(flags & f_invalid) ? count : 0;
    return outcome::success();
}

std::string file_info_t::serialize(bool include_blocks) const noexcept {
    return as_db(include_blocks).SerializeAsString();
}

void file_info_t::mark_local_available(size_t block_index) noexcept {
    assert(block_index < blocks.size());
    assert(!marks[block_index]);
    blocks[block_index]->mark_local_available(this);
    marks[block_index] = true;
    --missing_blocks;
}

bool file_info_t::is_locally_available(size_t block_index) const noexcept {
    assert(block_index < blocks.size());
    return marks[block_index];
}

bool file_info_t::is_locally_available() const noexcept { return missing_blocks == 0; }

bool file_info_t::is_partly_available() const noexcept { return missing_blocks < blocks.size(); }

void file_info_t::set_source(const file_info_ptr_t &peer_file) noexcept {
    if (peer_file) {
        auto &version = peer_file->get_version();
        auto sz = version.counters_size();
        assert(sz && "source file should have some version");
        auto &couter = version.counters(sz - 1);
        assert(couter.id());
        auto peer = peer_file->get_folder_info()->get_device();
        source_device = peer->device_id().get_sha256();
        source_version = peer_file->get_version();

    } else {
        source_device = {};
        source_version = {};
    }
}

file_info_ptr_t file_info_t::get_source() const noexcept {
    if (!source_device.empty()) {
        auto folder = get_folder_info()->get_folder();
        auto cluster = folder->get_cluster();
        auto peer_folder = folder->get_folder_infos().by_device_id(source_device);
        if (!peer_folder) {
            return {};
        }
        auto peer_file = peer_folder->get_file_infos().by_name(get_name());
        if (!peer_file) {
            return {};
        }
        if (compare(peer_file->version, source_version) != version_relation_t::identity) {
            return {};
        }
        return peer_file;
    }
    return {};
}

const boost::filesystem::path &file_info_t::get_path() const noexcept {
    if (!path) {
        path = folder_info->get_folder()->get_path() / std::string(get_name());
    }
    return path.value();
}

auto file_info_t::local_file() noexcept -> file_info_ptr_t {
    auto device = folder_info->get_device();
    auto cluster = folder_info->get_folder()->get_cluster();
    auto &my_device = cluster->get_device();
    assert(*device != *my_device);
    auto my_folder_info = folder_info->get_folder()->get_folder_infos().by_device(my_device);
    if (!my_folder_info) {
        return {};
    }

    auto local_file = my_folder_info->get_file_infos().by_name(get_name());
    if (!local_file) {
        return {};
    }

    auto source = local_file->get_source();
    if (source.get() == this) {
        return local_file;
    }

    return {};
}

void file_info_t::unlock() noexcept { flags = flags & ~flags_t::f_locked; }

void file_info_t::lock() noexcept { flags |= flags_t::f_locked; }

bool file_info_t::is_locked() const noexcept { return flags & flags_t::f_locked; }

void file_info_t::locally_unlock() noexcept { flags = flags & ~flags_t::f_local_locked; }

void file_info_t::locally_lock() noexcept { flags |= flags_t::f_local_locked; }

bool file_info_t::is_locally_locked() const noexcept { return flags & flags_t::f_local_locked; }

void file_info_t::assign_block(const model::block_info_ptr_t &block, size_t index) noexcept {
    assert(index < blocks.size() && "blocks should be reserve enough space");
    assert(!blocks[index]);
    blocks[index] = block;
    block->link(this, index);
}

bool file_info_t::check_consistency() noexcept {
    for (auto &b : blocks) {
        if (!b) {
            return false;
        }
    }
    return true;
}

void file_info_t::remove_blocks() noexcept {
    for (auto &it : blocks) {
        remove_block(it);
    }
    std::fill_n(marks.begin(), blocks.size(), false);
    missing_blocks = blocks.size();
}

void file_info_t::remove_block(block_info_ptr_t &block) noexcept {
    if (!block) {
        return;
    }
    auto indices = block->unlink(this, true);
    for (auto i : indices) {
        blocks[i] = nullptr;
    }
}

bool file_info_t::need_download(const file_info_t &other) noexcept {
    assert(folder_info->get_device() == folder_info->get_folder()->get_cluster()->get_device().get());
    assert(other.folder_info->get_device() != folder_info->get_folder()->get_cluster()->get_device().get());
    assert(name == other.name);
    if (is_locked()) {
        return false;
    }
    auto r = compare(version, other.version);
    if (r == version_relation_t::identity) {
        return !is_locally_available();
    } else if (r == version_relation_t::older) {
        return true;
    } else if (r == version_relation_t::newer) {
        return false;
    } else {
        assert(r == version_relation_t::conflict);
        auto log = utils::get_logger("model");
        LOG_CRITICAL(log, "conflict handling is not available");
        return false;
    }
}

file_info_ptr_t file_info_t::actualize() const noexcept { return folder_info->get_file_infos().get(get_uuid()); }

template <> SYNCSPIRIT_API std::string_view get_index<0>(const file_info_ptr_t &item) noexcept { return item->get_uuid(); }
template <> SYNCSPIRIT_API std::string_view get_index<1>(const file_info_ptr_t &item) noexcept { return item->get_name(); }

file_info_ptr_t file_infos_map_t::by_name(std::string_view name) noexcept { return get<1>(name); }

} // namespace syncspirit::model
