// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster.h"
#include "file_info.h"
#include "folder_info.h"
#include "device.h"
#include "db/prefix.h"
#include "fs/utils.h"
#include "misc/error_code.h"
#include "misc/file_iterator.h"
#include "proto/proto-helpers.h"
#include "proto/proto-helpers.h"
#include "utils/bytes_comparator.hpp"
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/date_time.hpp>
#include <algorithm>
#include <set>

namespace syncspirit::model {

namespace pt = boost::posix_time;

static const constexpr char prefix = (char)(db::prefix::file_info);

auto file_info_t::decompose_key(utils::bytes_view_t key) -> decomposed_key_t {
    assert(key.size() == file_info_t::data_length);
    auto fi_key = key.subspan(1, uuid_length);
    auto file_id = key.subspan(1 + uuid_length);
    return {fi_key, file_id};
}

outcome::result<file_info_ptr_t> file_info_t::create(utils::bytes_view_t key, const db::FileInfo &data,
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

    auto r = ptr->fields_update(info_);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

static void fill(unsigned char *key, const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    key[0] = prefix;
    auto fi_key = folder_info_->get_uuid();
    std::copy(fi_key.begin(), fi_key.end(), key + 1);
    std::copy(uuid.begin(), uuid.end(), key + 1 + fi_key.size());
}

file_info_t::guard_t::guard_t(file_info_t &file_) noexcept : file{&file_} { file_.synchronizing_lock(); }

file_info_t::guard_t::~guard_t() { file->synchronizing_unlock(); }

file_info_t::file_info_t(utils::bytes_view_t key_, const folder_info_ptr_t &folder_info_) noexcept
    : folder_info{folder_info_.get()} {
    assert(key_.subspan(1, uuid_length) == folder_info->get_uuid());
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

utils::bytes_t file_info_t::create_key(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    utils::bytes_t key;
    key.resize(data_length);
    fill(key.data(), uuid, folder_info_);
    return key;
}

std::string_view file_info_t::get_name() const noexcept { return name; }

std::uint64_t file_info_t::get_block_offset(size_t block_index) const noexcept {
    assert(!blocks.empty());
    return block_size * block_index;
}

auto file_info_t::fields_update(const db::FileInfo &source) noexcept -> outcome::result<void> {
    name = db::get_name(source);
    sequence = db::get_sequence(source);
    type = db::get_type(source);
    set_size(db::get_size(source));
    permissions = db::get_permissions(source);
    modified_s = db::get_modified_s(source);
    modified_ns = db::get_modified_ns(source);
    modified_by = db::get_modified_by(source);
    if (db::get_deleted(source)) {
        flags |= flags_t::f_deleted;
    }
    if (db::get_invalid(source)) {
        flags |= flags_t::f_invalid;
    }
    if (db::get_no_permissions(source)) {
        flags |= flags_t::f_no_permissions;
    }

    symlink_target = db::get_symlink_target(source);

    version.reset(new version_t(db::get_version(source)));

    full_name = fmt::format("{}/{}", folder_info->get_folder()->get_label(), get_name());
    block_size = size ? db::get_blocks_size(source) : 0;
    return reserve_blocks(size ? block_size : 0);
}

auto file_info_t::fields_update(const proto::FileInfo &source) noexcept -> outcome::result<void> {
    name = proto::get_name(source);
    sequence = proto::get_sequence(source);
    type = proto::get_type(source);
    set_size(proto::get_size(source));
    permissions = proto::get_permissions(source);
    modified_s = proto::get_modified_s(source);
    modified_ns = proto::get_modified_ns(source);
    modified_by = proto::get_modified_by(source);
    if (proto::get_deleted(source)) {
        flags |= flags_t::f_deleted;
    }
    if (proto::get_invalid(source)) {
        flags |= flags_t::f_invalid;
    }
    if (proto::get_no_permissions(source)) {
        flags |= flags_t::f_no_permissions;
    }

    symlink_target = proto::get_symlink_target(source);

    version.reset(new version_t(proto::get_version(source)));

    full_name = fmt::format("{}/{}", folder_info->get_folder()->get_label(), get_name());
    block_size = size ? proto::get_block_size(source) : 0;
    return reserve_blocks(size ? block_size : 0);
}

utils::bytes_view_t file_info_t::get_uuid() const noexcept { return {key + 1 + uuid_length, uuid_length}; }

void file_info_t::set_sequence(std::int64_t value) noexcept { sequence = value; }

db::FileInfo file_info_t::as_db(bool include_blocks) const noexcept {
    auto r = db::FileInfo();
    db::set_name(r, name);
    db::set_sequence(r, sequence);
    db::set_type(r, type);
    db::set_size(r, size);
    db::set_permissions(r, permissions);
    db::set_modified_s(r, modified_s);
    db::set_modified_ns(r, modified_ns);
    db::set_modified_by(r, modified_by);
    db::set_deleted(r, flags & f_deleted);
    db::set_invalid(r, flags & f_invalid);
    db::set_no_permissions(r, flags & f_no_permissions);
    db::set_version(r, version->as_proto());
    db::set_block_size(r, block_size);
    db::set_symlink_target(r, symlink_target);
    if (include_blocks) {
        for (auto &block : blocks) {
            if (!block) {
                continue;
            }
            db::add_blocks(r, block->get_hash());
        }
    }
    return r;
}

proto::FileInfo file_info_t::as_proto(bool include_blocks) const noexcept {
    auto r = proto::FileInfo();
    proto::set_name(r, name);
    proto::set_sequence(r, sequence);
    proto::set_type(r, type);
    proto::set_size(r, size);
    proto::set_permissions(r, permissions);
    proto::set_modified_s(r, modified_s);
    proto::set_modified_ns(r, modified_ns);
    proto::set_modified_by(r, modified_by);
    proto::set_deleted(r, flags & f_deleted);
    proto::set_invalid(r, flags & f_invalid);
    proto::set_no_permissions(r, flags & f_no_permissions);
    proto::set_version(r, version->as_proto());
    proto::set_block_size(r, block_size);
    proto::set_symlink_target(r, symlink_target);
    if (include_blocks) {
        size_t offset = 0;
        for (auto &b : blocks) {
            auto &block = *b;
            proto::add_blocks(r, block.as_bep(offset));
            offset += block.get_size();
        }
        if (blocks.empty() && is_file() && !is_deleted()) {
            unsigned char digest[SHA256_DIGEST_LENGTH];
            unsigned char empty_data[1] = {0};
            auto emtpy_block = proto::BlockInfo();
            auto weak_hash = adler32(0L, Z_NULL, 0);
            weak_hash = adler32(weak_hash, empty_data, 0);
            proto::set_weak_hash(emtpy_block, weak_hash);
            utils::digest(empty_data, 0, digest);
            auto digets_bytes = utils::bytes_view_t(digest, SHA256_DIGEST_LENGTH);
            proto::set_hash(emtpy_block, digets_bytes);
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

utils::bytes_t file_info_t::serialize(bool include_blocks) const noexcept {
    return db::encode::encode(as_db(include_blocks));
}

void file_info_t::mark_unreachable(bool value) noexcept {
    if (value) {
        flags |= f_unreachable;
    } else {
        flags &= ~f_unreachable;
    }
}

void file_info_t::mark_local() noexcept {
    flags = flags | f_local;
    auto self = folder_info->get_device();
    auto folder = folder_info->get_folder();
    for (auto it : folder->get_folder_infos()) {
        auto fi = it.item.get();
        auto peer = fi->get_device();
        if (peer != self) {
            auto fit = peer->get_iterator();
            if (fit) {
                auto peer_file = fi->get_file_infos().by_name(name);
                if (peer_file) {
                    fit->recheck(*peer_file);
                }
            }
        }
    }
}

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

const std::filesystem::path &file_info_t::get_path() const noexcept {
    if (!path) {
        path = folder_info->get_folder()->get_path() / boost::nowide::widen(name);
    }
    return path.value();
}

auto file_info_t::local_file() const noexcept -> file_info_ptr_t {
    auto device = folder_info->get_device();
    auto cluster = folder_info->get_folder()->get_cluster();
    auto &my_device = *cluster->get_device();
    assert(*device != my_device);
    (void)device;
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

std::size_t file_info_t::expected_meta_size() const noexcept {
    auto r = name.size() + 1 + 8 + 4 + 8 + 4 + 8 + 3 + 8 + 4 + symlink_target.size();
    r += version->counters_size() * 16;
    r += blocks.size() * (8 + 4 + 4 + 32);
    return r;
}

std::uint32_t file_info_t::get_permissions() const noexcept { return permissions; }

bool file_info_t::has_no_permissions() const noexcept { return flags & f_no_permissions; }

void file_info_t::update(const file_info_t &other) noexcept {
    using hashes_t = std::set<utils::bytes_view_t, utils::bytes_comparator_t>;
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
                    local_block_hashes.insert(b->get_hash());
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
        auto other = file->get_version();
        if (!version->contains(*other)) {
            return false;
        }
    }
    return true;
}

std::string file_info_t::make_conflicting_name() const noexcept {
    using adjustor_t = boost::date_time::c_local_adjustor<pt::ptime>;
    auto path = bfs::path(name);
    auto file_name = path.filename();
    auto stem = file_name.stem().string();
    auto ext = file_name.extension().string();
    auto utc = pt::from_time_t(modified_s);
    auto local = adjustor_t::utc_to_local(utc);
    auto ymd = local.date().year_month_day();
    auto time = local.time_of_day();
    auto &counter = version->get_best();
    auto device_short = device_id_t::make_short(proto::get_id(counter));
    auto conflicted_name =
        fmt::format("{}.sync-conflict-{:04}{:02}{:02}-{:02}{:02}{:02}-{}{}", stem, (int)ymd.year, ymd.month.as_number(),
                    ymd.day.as_number(), time.hours(), time.minutes(), time.seconds(), device_short, ext);
    auto full_name = path.parent_path() / conflicted_name;
    return full_name.string();
}

auto file_info_t::guard() noexcept -> guard_ptr_t { return new guard_t(*this); }

template <> SYNCSPIRIT_API std::string_view get_index<0>(const file_info_ptr_t &item) noexcept {
    auto bytes = item->get_uuid();
    auto ptr = (const char*) bytes.data();
    return {ptr, bytes.size()};
}
template <> SYNCSPIRIT_API std::string_view get_index<1>(const file_info_ptr_t &item) noexcept {
    return item->get_name();
}

template <> SYNCSPIRIT_API std::int64_t get_index<2>(const file_info_ptr_t &item) noexcept {
    return item->get_sequence();
}

auto file_infos_map_t::sequence_projection() noexcept -> seq_projection_t { return key2item.template get<2>(); }

file_info_ptr_t file_infos_map_t::by_uuid(utils::bytes_view_t uuid) noexcept {
    auto ptr = (const char*)uuid.data();
    return get<0>({ptr, uuid.size()});
}

file_info_ptr_t file_infos_map_t::by_name(std::string_view name) noexcept { return get<1>(name); }

file_info_ptr_t file_infos_map_t::by_sequence(std::int64_t value) noexcept { return get<2>(value); }

auto file_infos_map_t::range(std::int64_t lower, std::int64_t upper) noexcept -> range_t {
    auto &proj = sequence_projection();
    auto begin = proj.lower_bound(lower);
    auto end = proj.upper_bound(upper);
    return std::make_pair(begin, end);
}

} // namespace syncspirit::model
