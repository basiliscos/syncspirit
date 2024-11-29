// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "folder_info.h"
#include "file_info.h"
#include "cluster.h"
#include "misc/error_code.h"
#include "misc/version_utils.h"
#include "utils/log.h"
#include "utils/string_comparator.hpp"
#include "db/prefix.h"
#include "fs/utils.h"
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::file_info);

auto file_info_t::decompose_key(std::string_view key) -> decomposed_key_t {
    assert(key.size() == file_info_t::data_length);
    auto fi_key = key.substr(1, uuid_length);
    auto file_id = key.substr(1 + uuid_length);
    return {fi_key, file_id};
}

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

auto file_info_t::create(const bu::uuid &uuid_, const proto::FileInfo &info_,
                         const folder_info_ptr_t &folder_info_) noexcept -> outcome::result<file_info_ptr_t> {
    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(uuid_, folder_info_);

    auto r = ptr->fields_update(info_, info_.blocks_size());
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

static void fill(char *key, const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    key[0] = prefix;
    auto fi_key = folder_info_->get_uuid();
    std::copy(fi_key.begin(), fi_key.end(), key + 1);
    std::copy(uuid.begin(), uuid.end(), key + 1 + fi_key.size());
}

file_info_t::guard_t::guard_t(file_info_t &file_) noexcept : file{&file_} { file_.synchronizing_lock(); }

file_info_t::guard_t::~guard_t() { file->synchronizing_unlock(); }

file_info_t::file_info_t(std::string_view key_, const folder_info_ptr_t &folder_info_) noexcept
    : folder_info{folder_info_.get()} {
    assert(key_.substr(1, uuid_length) == folder_info->get_uuid());
    std::copy(key_.begin(), key_.end(), key);
}

file_info_t::file_info_t(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept
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

std::string file_info_t::create_key(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    std::string key;
    key.resize(data_length);
    fill(key.data(), uuid, folder_info_);
    return key;
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
    symlink_target = s.symlink_target();
    version = s.version();
    full_name = fmt::format("{}/{}", folder_info->get_folder()->get_label(), get_name());
    block_size = size ? s.block_size() : 0;
    return reserve_blocks(size ? block_count : 0);
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
    return r;
}

proto::FileInfo file_info_t::as_proto(bool include_blocks) const noexcept {
    auto r = as<proto::FileInfo>();
    if (include_blocks) {
        size_t offset = 0;
        for (auto &b : blocks) {
            auto &block = *b;
            *r.add_blocks() = block.as_bep(offset);
            offset += block.get_size();
        }
        if (blocks.empty() && is_file() && !is_deleted()) {
            auto emtpy_block = r.add_blocks();
            auto data = std::string();
            auto weak_hash = adler32(0L, Z_NULL, 0);
            weak_hash = adler32(weak_hash, (const unsigned char *)data.data(), data.length());
            emtpy_block->set_weak_hash(weak_hash);

            char digest[SHA256_DIGEST_LENGTH];
            utils::digest(data.data(), data.length(), digest);
            emtpy_block->set_hash(std::string(digest, SHA256_DIGEST_LENGTH));
        }
    }
    return r;
}

outcome::result<void> file_info_t::reserve_blocks(size_t block_count) noexcept {
    size_t count = 0;
    if (!block_count && !(flags & f_deleted) && !(flags & f_invalid)) {
        if ((size < block_size) && (size >= (int64_t)fs::block_sizes[0])) {
            return make_error_code(error_code_t::invalid_block_size);
        }
        if (size) {
            if (!block_size) {
                return make_error_code(error_code_t::invalid_block_size);
            }
            count = size / block_size;
            if ((int64_t)(block_size * count) != size) {
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

void file_info_t::mark_unreachable(bool value) noexcept {
    if (value) {
        flags |= f_unreachable;
    } else {
        flags &= ~f_unreachable;
    }
}

void file_info_t::mark_local() noexcept { flags = flags | f_local; }

void file_info_t::mark_local_available(size_t block_index) noexcept {
    assert(block_index < blocks.size());
    assert(!marks[block_index]);
    assert(missing_blocks);
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

const boost::filesystem::path &file_info_t::get_path() const noexcept {
    if (!path) {
        path = folder_info->get_folder()->get_path() / std::string(get_name());
    }
    return path.value();
}

auto file_info_t::local_file() const noexcept -> file_info_ptr_t {
    auto device = folder_info->get_device();
    auto cluster = folder_info->get_folder()->get_cluster();
    auto &my_device = *cluster->get_device();
    assert(*device != my_device);
    auto my_folder_info = folder_info->get_folder()->get_folder_infos().by_device(my_device);
    if (!my_folder_info) {
        return {};
    }

    auto local_file = my_folder_info->get_file_infos().by_name(get_name());
    return local_file;
}

void file_info_t::unlock() noexcept { flags = flags & ~flags_t::f_locked; }

void file_info_t::lock() noexcept { flags |= flags_t::f_locked; }

bool file_info_t::is_locked() const noexcept { return flags & flags_t::f_locked; }

void file_info_t::synchronizing_unlock() noexcept { flags = flags & ~flags_t::f_synchronizing; }

void file_info_t::synchronizing_lock() noexcept { flags |= flags_t::f_synchronizing; }

bool file_info_t::is_synchronizing() const noexcept { return flags & flags_t::f_synchronizing; }

bool file_info_t::is_unlocking() const noexcept { return flags & flags_t::f_unlocking; }

void file_info_t::set_unlocking(bool value) noexcept {
    if (value) {
        flags |= flags_t::f_unlocking;
    } else {
        flags = flags & ~flags_t::f_unlocking;
    }
}

void file_info_t::assign_block(const model::block_info_ptr_t &block, size_t index) noexcept {
    assert(index < blocks.size() && "blocks should be reserve enough space");
    assert(!blocks[index]);
    blocks[index] = block;
    block->link(this, index);
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
    auto indices = block->unlink(this);
    for (auto i : indices) {
        blocks[i] = nullptr;
    }
}

std::int64_t file_info_t::get_size() const noexcept {
    if (type == proto::FileInfoType::FILE) {
        bool ok = !is_deleted() && !is_invalid();
        if (ok) {
            return size;
        }
    }
    return 0;
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
        auto stringify = [](const proto::Vector &vector) -> std::string {
            auto r = std::string();
            for (int i = 0; i < vector.counters_size(); ++i) {
                auto &c = vector.counters(i);
                r += fmt::format("{:x}:{}", c.id(), c.value());
                if (i + 1 < vector.counters_size()) {
                    r += ", ";
                }
            }
            return r;
        };
        auto my_version = stringify(version);
        auto other_version = stringify(other.version);

        auto log = utils::get_logger("model.file_info");
        LOG_CRITICAL(log, "conflict handling is not available for = {}, '{}' vs '{}'", get_full_name(), my_version,
                     other_version);
        return false;
    }
}

std::size_t file_info_t::expected_meta_size() const noexcept {
    auto r = name.size() + 1 + 8 + 4 + 8 + 4 + 8 + 3 + 8 + 4 + symlink_target.size();
    r += version.counters_size() * 16;
    r += blocks.size() * (8 + 4 + 4 + 32);
    return r;
}

std::uint32_t file_info_t::get_permissions() const noexcept { return permissions; }

bool file_info_t::has_no_permissions() const noexcept { return flags & f_no_permissions; }

void file_info_t::update(const file_info_t &other) noexcept {
    using hashes_t = std::set<std::string, utils::string_comparator_t>;
    assert(this->get_key() == other.get_key());
    assert(this->name == other.name);
    type = other.type;
    size = other.size;
    permissions = other.permissions;
    modified_s = other.modified_s;
    modified_ns = other.modified_ns;
    modified_by = other.modified_by;
    flags = (other.flags & 0b111) | (flags & ~0b111); // local flags are preserved
    version = other.version;
    sequence = other.sequence;
    block_size = other.block_size;
    symlink_target = other.symlink_target;

    auto local_block_hashes = hashes_t{};
    for (auto &b : blocks) {
        if (b) {
            for (auto &fb : b->get_file_blocks()) {
                if (fb.is_locally_available() && fb.file() == this) {
                    local_block_hashes.insert(std::string(b->get_hash()));
                    break;
                }
            }
        }
    }
    remove_blocks();

    marks = other.marks;
    blocks.resize(other.blocks.size());
    missing_blocks = blocks.size();
    for (size_t i = 0; i < other.blocks.size(); ++i) {
        auto &b = other.blocks[i];
        if (b) {
            assign_block(b, i);
            auto already_local = marks[i];
            if (already_local) {
                --missing_blocks;
            } else if (local_block_hashes.contains(b->get_hash())) {
                mark_local_available(i);
            }
        }
    }
}

bool file_info_t::is_global() const noexcept {
    using V = version_relation_t;
    auto self = folder_info->get_device();
    auto folder = folder_info->get_folder();
    for (auto &it : folder->get_folder_infos()) {
        if (it.item->get_device() == self) {
            continue;
        }
        auto &files = it.item->get_file_infos();
        auto file = files.by_name(name);
        if (!file) {
            continue;
        }
        auto &v_other = file->get_version();
        auto v_result = compare(version, v_other);
        if (v_result == V::identity || v_result == V::newer) {
            continue;
        }
        return false;
    }
    return true;
}

auto file_info_t::guard() noexcept -> guard_ptr_t { return new guard_t(*this); }

template <> SYNCSPIRIT_API std::string_view get_index<0>(const file_info_ptr_t &item) noexcept {
    return item->get_uuid();
}
template <> SYNCSPIRIT_API std::string_view get_index<1>(const file_info_ptr_t &item) noexcept {
    return item->get_name();
}

template <> SYNCSPIRIT_API std::int64_t get_index<2>(const file_info_ptr_t &item) noexcept {
    return item->get_sequence();
}

file_info_ptr_t file_infos_map_t::by_name(std::string_view name) noexcept { return get<1>(name); }

file_info_ptr_t file_infos_map_t::by_sequence(std::int64_t value) noexcept { return get<2>(value); }

auto file_infos_map_t::range(std::int64_t lower, std::int64_t upper) noexcept -> range_t {
    auto &proj = key2item.template get<2>();
    auto begin = proj.lower_bound(lower);
    auto end = proj.upper_bound(upper);
    return std::make_pair(begin, end);
}

} // namespace syncspirit::model
