// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster.h"
#include "file_info.h"
#include "folder_info.h"
#include "device.h"
#include "db/prefix.h"
#include "misc/error_code.h"
#include "misc/file_iterator.h"
#include "proto/proto-helpers.h"
#include "utils/bytes_comparator.hpp"
#include <spdlog/spdlog.h>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/date_time.hpp>
#include <algorithm>
#include <set>

namespace syncspirit::model {

static const auto empty_block = []() -> proto::BlockInfo {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned char empty_data[1] = {0};
    utils::digest(empty_data, 0, digest);
    auto digets_bytes = utils::bytes_view_t(digest, SHA256_DIGEST_LENGTH);
    auto block = proto::BlockInfo();
    proto::set_hash(block, digets_bytes);
    return block;
}();

namespace pt = boost::posix_time;

static const constexpr char prefix = (char)(db::prefix::file_info);

static inline proto::FileInfoType as_type(std::uint16_t flags) noexcept {
    using T = proto::FileInfoType;
    return flags & file_info_t::f_type_dir ? T::DIRECTORY : flags & file_info_t::f_type_link ? T::SYMLINK : T::FILE;
}

auto file_info_t::decompose_key(utils::bytes_view_t key) -> decomposed_key_t {
    assert(key.size() == file_info_t::data_length + 1);
    auto fi_key = key.subspan(1, uuid_length);
    auto file_id = key.subspan(1 + uuid_length);
    return {fi_key, file_id};
}

outcome::result<file_info_ptr_t> file_info_t::create(utils::bytes_view_t key, const db::FileInfo &data,
                                                     const folder_info_ptr_t &folder_info) noexcept {
    if (key.size() != data_length + 1) {
        return make_error_code(error_code_t::invalid_file_info_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_file_info_prefix);
    }

    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(key, folder_info);

    auto &path_cache = folder_info->get_folder()->get_cluster()->get_path_cache();
    auto r = ptr->fields_update(data, path_cache);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

auto file_info_t::create(const bu::uuid &uuid_, const proto::FileInfo &info_,
                         const folder_info_ptr_t &folder_info) noexcept -> outcome::result<file_info_ptr_t> {
    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(uuid_, folder_info);

    auto &path_cache = folder_info->get_folder()->get_cluster()->get_path_cache();
    auto r = ptr->fields_update(info_, path_cache);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

static void fill(unsigned char *key, const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    auto fi_uuid = folder_info_->get_uuid();
    std::copy(fi_uuid.begin(), fi_uuid.end(), key);
    std::copy(uuid.begin(), uuid.end(), key + fi_uuid.size());
}

file_info_t::guard_t::guard_t(file_info_t &file_, const folder_info_t *folder_info_) noexcept
    : file{&file_}, folder_info{folder_info_} {
    file_.synchronizing_lock();
    auto cluster = folder_info->get_folder()->get_cluster();
    path_guard = std::make_unique<path_guard_t>(cluster->lock(file->get_name().get()));
    assert(path_guard && *path_guard);
}

file_info_t::guard_t::~guard_t() {
    if (file) {
        file->synchronizing_unlock();
    }
}

file_info_t::blocks_iterator_t::blocks_iterator_t(const file_info_t *file_, std::uint32_t start_index_) noexcept
    : file{file_}, next_index{start_index_} {}

const block_info_t *file_info_t::blocks_iterator_t::next() noexcept {
    auto r = (const block_info_t *)(nullptr);
    if (file && file->is_file()) {
        auto &file_content = file->content.file;
        auto &blocks = file_content.blocks;
        if (next_index < static_cast<std::uint32_t>(blocks.size())) {
            auto ptr = reinterpret_cast<std::uintptr_t>(blocks[next_index]);
            r = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
            ++next_index;
        }
    }
    return r;
}

auto file_info_t::blocks_iterator_t::current() const noexcept -> indexed_block_t {
    auto r = indexed_block_t{nullptr, next_index};
    if (file && file->is_file()) {
        auto &blocks = file->content.file.blocks;
        if (next_index < static_cast<std::uint32_t>(blocks.size())) {
            auto ptr = reinterpret_cast<std::uintptr_t>(blocks[next_index]);
            r.first = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
        }
    }
    return r;
}

std::uint32_t file_info_t::blocks_iterator_t::get_total() const noexcept {
    std::uint32_t r = 0;
    if (file && file->is_file()) {
        r = static_cast<std::uint32_t>(file->content.file.blocks.size());
    }
    return r;
}

file_info_t::content_t::content_t() {}
file_info_t::content_t::~content_t() {}

file_info_t::size_full_t::~size_full_t() {}

file_info_t::size_less_t::~size_less_t() {}

file_info_t::file_info_t(utils::bytes_view_t key_, const folder_info_ptr_t &folder_info) noexcept {
    auto uuid_from_key = key_.subspan(1);
    assert(uuid_from_key.subspan(0, uuid_length) == folder_info->get_uuid());
    std::copy(uuid_from_key.begin(), uuid_from_key.end(), key);
}

file_info_t::file_info_t(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    fill(key, uuid, folder_info_);
}

file_info_t::~file_info_t() {
    if (flags & f_type_file) {
        auto &blocks = content.file.blocks;
        for (auto b_raw : blocks) {
            if (!b_raw) {
                continue;
            }
            auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
            auto block = reinterpret_cast<block_info_t *>(ptr & PTR_MASK);
            auto indices = block->unlink(this);
            for (auto i : indices) {
                auto p = reinterpret_cast<std::uintptr_t>(blocks[i]);
                auto b = reinterpret_cast<block_info_t *>(p & PTR_MASK);
                intrusive_ptr_release(b);
                blocks[i] = {};
            }
        }
        content.file.~size_full_t();
    } else {
        content.non_file.~size_less_t();
    }
    name.reset();
}

utils::bytes_t file_info_t::create_key(const bu::uuid &uuid, const folder_info_ptr_t &folder_info_) noexcept {
    utils::bytes_t key;
    key.resize(data_length);
    fill(key.data(), uuid, folder_info_);
    return key;
}

auto file_info_t::get_name() const noexcept -> const path_ptr_t & { return name; }

std::uint64_t file_info_t::get_block_offset(size_t block_index) const noexcept {
    assert(flags & f_type_file && !content.file.blocks.empty());
    auto ptr = reinterpret_cast<std::uintptr_t>(content.file.blocks.front());
    auto block = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
    return block->get_size() * block_index;
}

auto file_info_t::fields_update(const db::FileInfo &source, model::path_cache_t &path_cache) noexcept
    -> outcome::result<void> {
    flags = (flags & ~0b111111) | as_flags(db::get_type(source));
    name = path_cache.get_path(db::get_name(source));
    sequence = db::get_sequence(source);
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

    version = version_t(db::get_version(source));

    if (flags & f_type_file) {
        new (&content.file) size_full_t();
        auto declared_size = db::get_size(source);
        bool has_content = declared_size && (flags & f_type_file);
        auto block_size = db::get_blocks_size(source);
        auto blocks_count = has_content ? block_size : 0;
        if (blocks_count) {
            reserve_blocks(blocks_count);
        }
    } else {
        new (&content.non_file) size_less_t();
        auto link = db::get_symlink_target(source);
        auto ptr = link.data();
        content.non_file.symlink_target = string_t(ptr, ptr + link.size());
    }
    return outcome::success();
}

auto file_info_t::fields_update(const proto::FileInfo &source, model::path_cache_t &path_cache) noexcept
    -> outcome::result<void> {
    name = path_cache.get_path(proto::get_name(source));
    sequence = proto::get_sequence(source);
    flags = (flags & ~0b111111) | as_flags(proto::get_type(source));
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

    version = version_t(proto::get_version(source));
    if (flags & f_type_file) {
        new (&content.file) size_full_t();
        auto declared_size = proto::get_size(source);
        bool has_content = declared_size && (flags & f_type_file);
        auto block_size = proto::get_blocks_size(source);
        auto blocks_count = has_content ? block_size : 0;
        if (blocks_count) {
            reserve_blocks(blocks_count);
        }
    } else {
        new (&content.non_file) size_less_t();
        auto link = proto::get_symlink_target(source);
        auto ptr = link.data();
        content.non_file.symlink_target = string_t(ptr, ptr + link.size());
    }
    return outcome::success();
}

utils::bytes_view_t file_info_t::get_uuid() const noexcept { return {key + uuid_length, uuid_length}; }
utils::bytes_view_t file_info_t::get_full_id() const noexcept { return {key, data_length}; }
utils::bytes_view_t file_info_t::get_folder_uuid() const noexcept { return utils::bytes_view_t(key, uuid_length); }

void file_info_t::set_sequence(std::int64_t value) noexcept { sequence = value; }

db::FileInfo file_info_t::as_db(bool include_blocks) const noexcept {
    auto r = db::FileInfo();
    db::set_name(r, name->get_full_name());
    db::set_sequence(r, sequence);
    db::set_type(r, as_type(flags));
    db::set_permissions(r, permissions);
    db::set_modified_s(r, modified_s);
    db::set_modified_ns(r, modified_ns);
    db::set_modified_by(r, modified_by);
    db::set_deleted(r, flags & f_deleted);
    db::set_invalid(r, flags & f_invalid);
    db::set_no_permissions(r, flags & f_no_permissions);
    db::set_version(r, version.as_proto());
    if (flags & f_type_file) {
        db::set_size(r, get_size());
        db::set_block_size(r, get_block_size());
        if (include_blocks) {
            for (auto b_raw : content.file.blocks) {
                auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
                auto block = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
                if (!block) {
                    continue;
                }
                db::add_blocks(r, block->get_hash());
            }
        }
    } else {
        auto &container = content.non_file.symlink_target;
        auto ptr = container.data();
        auto link = std::string_view(ptr, ptr + container.size());
        db::set_symlink_target(r, link);
    }
    return r;
}

proto::FileInfo file_info_t::as_proto(bool include_blocks) const noexcept {
    auto r = proto::FileInfo();
    proto::set_name(r, name->get_full_name());
    proto::set_sequence(r, sequence);
    proto::set_type(r, as_type(flags));
    proto::set_permissions(r, permissions);
    proto::set_modified_s(r, modified_s);
    proto::set_modified_ns(r, modified_ns);
    proto::set_modified_by(r, modified_by);
    proto::set_deleted(r, flags & f_deleted);
    proto::set_invalid(r, flags & f_invalid);
    proto::set_no_permissions(r, flags & f_no_permissions);
    proto::set_version(r, version.as_proto());

    if (flags & f_type_file) {
        proto::set_size(r, get_size());
        proto::set_block_size(r, get_block_size());
        if (include_blocks) {
            size_t offset = 0;
            for (auto b_raw : content.file.blocks) {
                auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
                auto block = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
                proto::add_blocks(r, block->as_bep(offset));
                offset += block->get_size();
            }
            if (content.file.blocks.empty() && !(flags & f_deleted)) {
                proto::add_blocks(r, empty_block);
            }
        }
    } else {
        auto &container = content.non_file.symlink_target;
        auto ptr = container.data();
        auto link = std::string_view(ptr, ptr + container.size());
        proto::set_symlink_target(r, link);
    }
    return r;
}

void file_info_t::reserve_blocks(size_t block_count) noexcept {
    remove_blocks();
    content.file.blocks.resize(block_count);
}

utils::bytes_t file_info_t::serialize(bool include_blocks) const noexcept { return db::encode(as_db(include_blocks)); }

void file_info_t::mark_unreachable(bool value) noexcept {
    if (value) {
        flags |= f_unreachable;
    } else {
        flags &= ~f_unreachable;
    }
}

void file_info_t::mark_local(bool available, const folder_info_t &folder_info) noexcept {
    if (available) {
        flags = flags | f_local;
    } else {
        flags = flags & ~f_local;
        flags = flags & ~f_available;
    }
    if (available) {
        auto available = std::uint32_t{0};
        if (flags & f_type_file) {
            for (auto b_raw : content.file.blocks) {
                auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
                if (ptr & LOCAL_MASK) {
                    ++available;
                }
            }
        }
        if (!(flags & f_type_file) || static_cast<std::uint32_t>(content.file.blocks.size()) == available) {
            flags = flags | f_available;
        }

        auto self = folder_info.get_device();
        auto folder = folder_info.get_folder();
        for (auto it : folder->get_folder_infos()) {
            auto fi = it.item.get();
            auto peer = fi->get_device();
            if (peer != self) {
                auto fit = peer->get_iterator();
                if (fit) {
                    auto peer_file = fi->get_file_infos().by_name(name->get_full_name());
                    if (peer_file) {
                        fit->recheck(*fi, *peer_file);
                    }
                }
            }
        }
    }
}

void file_info_t::mark_local_available(size_t block_index) noexcept {
    auto &blocks = content.file.blocks;
    assert(block_index < blocks.size());

    auto ptr = reinterpret_cast<std::uintptr_t>(blocks[block_index]);
    assert(!(ptr & LOCAL_MASK));
    auto block = reinterpret_cast<block_info_t *>(ptr & PTR_MASK);
    block->mark_local_available(this);

    ptr = ptr | LOCAL_MASK;
    auto block_mangled = reinterpret_cast<block_info_t *>(ptr);
    blocks[block_index] = block_mangled;

    if (!(flags & f_available)) {
        auto available = std::uint32_t{0};
        for (auto b_raw : blocks) {
            auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
            if (ptr & LOCAL_MASK) {
                ++available;
            }
        }
        if (available == static_cast<std::uint32_t>(blocks.size())) {
            flags = flags | f_available;
        }
    }
}

bool file_info_t::is_locally_available(size_t block_index) const noexcept {
    auto &blocks = content.file.blocks;
    assert(block_index < blocks.size());
    auto ptr = reinterpret_cast<std::uintptr_t>(blocks[block_index]);
    return ptr & LOCAL_MASK;
}

bool file_info_t::is_locally_available() const noexcept {
    auto r = !(flags & f_type_file) || ((flags & f_available) || content.file.blocks.empty());
    return r;
};

const std::filesystem::path file_info_t::get_path(const folder_info_t &folder_info) const noexcept {
    auto own_name = boost::nowide::widen(name->get_full_name());
    auto path = folder_info.get_folder()->get_path() / own_name;
    path.make_preferred();
    return path;
}

void file_info_t::synchronizing_unlock() noexcept { flags = flags & ~flags_t::f_synchronizing; }

void file_info_t::synchronizing_lock() noexcept { flags |= flags_t::f_synchronizing; }

bool file_info_t::is_synchronizing() const noexcept { return flags & flags_t::f_synchronizing; }

void file_info_t::assign_block(model::block_info_t *block, size_t index) noexcept {
    auto &blocks = content.file.blocks;
    assert(flags & f_type_file);
    assert(index < blocks.size() && "blocks should be reserve enough space");
    assert(!blocks[index]);
    blocks[index] = block;
    intrusive_ptr_add_ref(block);
    block->link(this, index);
}

void file_info_t::remove_blocks() noexcept {
    if (flags & f_type_file) {
        for (auto b_raw : content.file.blocks) {
            auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
            auto block = reinterpret_cast<block_info_t *>(ptr & PTR_MASK);
            remove_block(block);
        }
        flags = flags & ~f_available;
    }
}

void file_info_t::remove_block(block_info_t *block) noexcept {
    assert(flags & f_type_file);
    if (!block) {
        return;
    }
    auto indices = block->unlink(this);
    for (auto i : indices) {
        content.file.blocks[i] = nullptr;
        intrusive_ptr_release(block);
    }
}

std::int64_t file_info_t::get_size() const noexcept {
    auto r = std::int64_t{0};
    if (flags & f_type_file) {
        if (!is_deleted() && !is_invalid()) {
            auto &blocks = content.file.blocks;
            auto blocks_count = blocks.size();
            if (blocks_count) {
                auto ptr_first = reinterpret_cast<std::uintptr_t>(blocks.front());
                auto first = reinterpret_cast<const block_info_t *>(ptr_first & PTR_MASK);
                auto ptr_last = reinterpret_cast<std::uintptr_t>(blocks.back());
                auto last = reinterpret_cast<const block_info_t *>(ptr_last & PTR_MASK);
                r = (blocks_count - 1) * first->get_size() + last->get_size();
            }
        }
    }
    return r;
}

std::size_t file_info_t::expected_meta_size() const noexcept {
    auto r = name->get_full_name().size() + 1 + 8 + 4 + 8 + 4 + 8 + 3 + 8 + 4;
    r += version.counters_size() * 16;
    if (flags & f_type_file) {
        r += content.file.blocks.size() * (8 + 4 + 4 + 32);
    } else {
        r += content.non_file.symlink_target.size();
    }
    return r;
}

std::uint32_t file_info_t::get_permissions() const noexcept { return permissions; }

bool file_info_t::has_no_permissions() const noexcept { return flags & f_no_permissions; }

void file_info_t::update(const file_info_t &other) noexcept {
    using hashes_t = std::set<utils::bytes_view_t, utils::bytes_comparator_t>;

    assert((flags & 0b111) == (other.flags & 0b111));

    assert(this->name == other.name);
    assert(this->name->get_full_name() == other.name->get_full_name());
    assert((this->get_uuid() == other.get_uuid()) || version.identical_to(other.version));
    permissions = other.permissions;
    modified_s = other.modified_s;
    modified_ns = other.modified_ns;
    modified_by = other.modified_by;
    flags = (other.flags & 0b111111) | (flags & ~0b111111); // local flags are preserved
    version = other.version;
    sequence = other.sequence;
    if (!(flags & f_type_file)) {
        content.non_file.symlink_target = other.content.non_file.symlink_target;
    } else {
        auto local_block_hashes = hashes_t{};
        for (auto b_raw : content.file.blocks) {
            if (b_raw) {
                auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
                auto block = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
                auto it = block->iterate_blocks();
                while (auto fb = it.next()) {
                    if (fb->is_locally_available() && fb->file() == this) {
                        local_block_hashes.emplace(block->get_hash());
                        break;
                    }
                }
            }
        }

        // avoid use after free, as local block hashes have block_views
        auto sz = other.content.file.blocks.size();
        auto blocks_copy = std::vector<model::block_info_ptr_t>();
        blocks_copy.reserve(content.file.blocks.size());
        for (auto b_raw : content.file.blocks) {
            auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
            auto block = reinterpret_cast<block_info_t *>(ptr & PTR_MASK);
            blocks_copy.emplace_back(block);
        }
        remove_blocks();

        content.file.blocks.resize(sz);
        for (size_t i = 0; i < sz; ++i) {
            auto b_raw = other.content.file.blocks[i];
            if (b_raw) {
                auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
                auto block = reinterpret_cast<block_info_t *>(ptr & PTR_MASK);
                assign_block(block, i);
                auto already_local = ptr & LOCAL_MASK;
                if (local_block_hashes.contains(block->get_hash())) {
                    mark_local_available(i);
                }
            }
        }
    }
}

std::string file_info_t::make_conflicting_name() const noexcept {
    using adjustor_t = boost::date_time::c_local_adjustor<pt::ptime>;
    auto own_name = boost::nowide::widen(name->get_full_name());
    auto path = bfs::path(own_name);
    auto file_name = path.filename();
    auto stem = file_name.stem().string();
    auto ext = file_name.extension().string();
    auto utc = pt::from_time_t(modified_s);
    auto local = adjustor_t::utc_to_local(utc);
    auto ymd = local.date().year_month_day();
    auto time = local.time_of_day();
    auto counter = version.get_best();
    auto device_short = device_id_t::make_short(proto::get_id(counter));
    auto conflicted_name =
        fmt::format("{}.sync-conflict-{:04}{:02}{:02}-{:02}{:02}{:02}-{}{}", stem, (int)ymd.year, ymd.month.as_number(),
                    ymd.day.as_number(), time.hours(), time.minutes(), time.seconds(), device_short, ext);
    auto full_name = path.parent_path() / conflicted_name;
    return full_name.string();
}

auto file_info_t::guard(const model::folder_info_t &folder_info) noexcept -> guard_t {
    return guard_t(*this, &folder_info);
}

bool file_info_t::identical_to(const proto::FileInfo &file) const noexcept {
    auto &v = proto::get_version(file);
    if (version.identical_to(v)) {
        auto blocks_sz = proto::get_blocks_size(file);
        if (blocks_sz == content.file.blocks.size()) {
            for (size_t i = 0; i < blocks_sz; ++i) {
                auto &block = proto::get_blocks(file, i);
                auto hash = proto::get_hash(block);
                auto b_raw = content.file.blocks[i];
                auto ptr = reinterpret_cast<std::uintptr_t>(b_raw);
                auto b = reinterpret_cast<const block_info_t *>(ptr & PTR_MASK);
                if (b->get_hash() != hash) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

void file_info_t::set_augmentation(augmentation_t &value) noexcept { extension = &value; }

void file_info_t::set_augmentation(augmentation_ptr_t value) noexcept { extension = std::move(value); }

augmentation_ptr_t &file_info_t::get_augmentation() noexcept { return extension; }

void file_info_t::notify_update() noexcept {
    if (extension) {
        extension->on_update();
    }
}

bool file_infos_map_t::put(const model::file_info_ptr_t &item, bool replace) noexcept {
    bool result = false;
    auto prev = file_info_ptr_t();
    {
        auto &proj = parent_t::template get<0>();
        auto [it, inserted] = proj.emplace(item);
        if (!inserted && replace) {
            prev = *it;
            proj.replace(it, item);
            inserted = true;
        }
        result = inserted;
    }
    {
        auto &proj = parent_t::template get<1>();
        auto [it, inserted] = proj.emplace(item);
        if (!inserted && replace) {
            proj.replace(it, item);
            inserted = true;
        }
        result = inserted;
    }
    {
        auto &proj = parent_t::template get<2>();
        auto [it, inserted] = proj.emplace(item);
        if (!inserted && replace) {
            proj.replace(it, item);
            inserted = true;
        }
        result = inserted;
    }
    return result;
}

void file_infos_map_t::remove(const model::file_info_ptr_t &item) noexcept {
    parent_t::template get<0>().erase(item->get_uuid());
    parent_t::template get<1>().erase(file_details::get_name(item));
    parent_t::template get<2>().erase(item->get_sequence());
}

file_info_ptr_t file_infos_map_t::by_uuid(utils::bytes_view_t uuid) const noexcept {
    auto &proj = parent_t::template get<0>();
    auto it = proj.find(uuid);
    return it != proj.end() ? *it : file_info_ptr_t();
}

file_info_ptr_t file_infos_map_t::by_name(std::string_view name) const noexcept {
    auto &proj = parent_t::template get<1>();
    auto it = proj.find(name);
    return it != proj.end() ? *it : file_info_ptr_t();
}

file_info_ptr_t file_infos_map_t::by_sequence(std::int64_t sequence) const noexcept {
    auto &proj = parent_t::template get<2>();
    auto it = proj.find(sequence);
    return it != proj.end() ? *it : file_info_ptr_t();
}

auto file_infos_map_t::sequence_projection() noexcept -> seq_projection_t & { return parent_t::template get<2>(); }

auto file_infos_map_t::range(std::int64_t lower, std::int64_t upper) noexcept -> range_t {
    auto &proj = sequence_projection();
    auto begin = proj.lower_bound(lower);
    auto end = proj.upper_bound(upper);
    return std::make_pair(begin, end);
}

} // namespace syncspirit::model
