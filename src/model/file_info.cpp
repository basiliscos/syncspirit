#include "folder_info.h"
#include "file_info.h"
#include "cluster.h"
#include "block_iterator.h"
#include <algorithm>
#include <boost/mpl/pair.hpp>
#include <spdlog/spdlog.h>

namespace syncspirit::model {

file_info_t::file_info_t(const db::FileInfo &info_, folder_info_t *folder_info_) noexcept : folder_info{folder_info_} {
    db_key = generate_db_key(info_.name(), *folder_info);
    fields_update(info_);
    auto &blocks_map = folder_info->get_folder()->get_cluster()->get_blocks();
    for (int i = 0; i < info_.blocks_keys_size(); ++i) {
        auto key = info_.blocks_keys(i);
        auto block = blocks_map.by_key(key);
        assert(block);
        block->link(this, i);
        blocks.emplace_back(std::move(block));
    }
    version = info_.version();
    full_name = fmt::format("{}/{}", folder_info->get_folder()->label(), get_name());
}

file_info_t::file_info_t(const proto::FileInfo &info_, folder_info_t *folder_info_) noexcept
    : folder_info{folder_info_} {
    db_key = generate_db_key(info_.name(), *folder_info);
    fields_update(info_);
    update_blocks(info_);
    version = info_.version();
    mark_dirty();
    full_name = fmt::format("{}/{}", folder_info->get_folder()->label(), get_name());
}

template <typename Source> void file_info_t::fields_update(const Source &s) noexcept {
    sequence = s.sequence();
    type = s.type();
    set_size(s.size());
    permissions = s.permissions();
    modified_s = s.modified_s();
    modified_ns = s.modified_ns();
    modified_by = s.modified_by();
    deleted = s.deleted();
    invalid = s.invalid();
    no_permissions = s.no_permissions();
    version = s.version();
    block_size = s.block_size();
    symlink_target = s.symlink_target();
}

std::string file_info_t::generate_db_key(const std::string &name, const folder_info_t &fi) noexcept {
    std::string dbk;
    auto fi_key = fi.get_db_key();
    assert(fi_key);
    dbk.resize(sizeof(fi_key) + name.size());
    char *ptr = dbk.data();
    auto fi_key_ptr = reinterpret_cast<char *>(&fi_key);
    std::copy(fi_key_ptr, fi_key_ptr + sizeof(fi_key), ptr);
    ptr += sizeof(fi_key);
    std::copy(name.begin(), name.end(), ptr);
    return dbk;
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

std::string_view file_info_t::get_name() const noexcept {
    const char *ptr = db_key.data();
    ptr += sizeof(std::uint64_t);
    return std::string_view(ptr, db_key.size() - sizeof(std::uint64_t));
}

db::FileInfo file_info_t::serialize(bool include_blocks) noexcept {
    db::FileInfo r;
    auto name = get_name();
    r.set_name(name.data(), name.size());
    r.set_sequence(sequence);
    r.set_type(type);
    r.set_size(size);
    r.set_permissions(permissions);
    r.set_modified_s(modified_s);
    r.set_modified_ns(modified_ns);
    r.set_modified_by(modified_by);
    r.set_deleted(deleted);
    r.set_invalid(invalid);
    r.set_no_permissions(no_permissions);
    *r.mutable_version() = version;
    r.set_block_size(block_size);
    r.set_symlink_target(symlink_target);

    if (include_blocks) {
        for (auto &block : blocks) {
            if (!block) {
                continue;
            }
            auto db_key = block->get_db_key();
            assert(db_key && "block have to be persisted first");
            r.mutable_blocks_keys()->Add(db_key);
        }
    }
    return r;
}

void file_info_t::update(const proto::FileInfo &remote_info) noexcept {
    if (remote_info.sequence() > sequence) {
        fields_update(remote_info);
        update_blocks(remote_info);
        mark_dirty();
    }
}

bool file_info_t::update(const local_file_t &local_file) noexcept {
    auto local_blocks = local_file.blocks;
    bool matched = local_blocks.size() == blocks.size();
    auto limit = std::min(local_blocks.size(), blocks.size());
    size_t i = 0;
    for (; i < limit; ++i) {
        auto &lb = local_blocks[i];
        auto &sb = blocks[i];
        if (!(*lb == *sb)) {
            matched = false;
            break;
        }
    }

    if (matched) {
        for (auto &b : blocks) {
            b->mark_local_available(this);
            ++i;
        }
    } else {
        size_t j = 0;
        if (!local_file.temp) {
            for (auto &b : blocks) {
                b->unlink(this, true);
            }
            blocks = local_blocks;
            for (auto &b : blocks) {
                b->link(this, j++);
                b->mark_local_available(this);
            }
        } else {
            for (j = 0; j < i; ++j) {
                auto &b = blocks[j];
                b->link(this, j++);
                b->mark_local_available(this);
            }
            for (j = i; j < blocks.size(); ++j) {
                blocks[j]->unlink(this, true);
                blocks[j] = nullptr;
            }
            mark_incomplete();
            matched = true;
        }
        mark_dirty();
    }
    return !matched;
}

bool file_info_t::is_incomplete() const noexcept { return incomplete; }

void file_info_t::mark_complete() noexcept { incomplete = false; }

void file_info_t::mark_incomplete() noexcept { incomplete = true; }

void file_info_t::update_blocks(const proto::FileInfo &remote_info) noexcept {
    auto &cluster = *folder_info->get_folder()->get_cluster();
    auto &blocks_map = cluster.get_blocks();

    auto ex_blocks = block_infos_map_t();
    for (auto &block : blocks) {
        ex_blocks.put(block);
    }

    blocks.resize(0);
    for (int i = 0; i < remote_info.blocks_size(); ++i) {
        auto &b = remote_info.blocks(i);
        auto &hash = b.hash();
        auto block = blocks_map.by_id(hash);
        if (!block) {
            block = new block_info_t(b);
            block->link(this, i);
        } else {
            auto ex_block = ex_blocks.by_id(hash);
            if (ex_block) {
                ex_blocks.remove(ex_block);
            } else {
                block->link(this, i);
            }
        }
        blocks.emplace_back(std::move(block));
    }
    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    for (auto &it : ex_blocks) {
        remove_block(it.second, blocks_map, deleted_blocks_map, false);
    }
}

void file_info_t::remove_block(block_info_ptr_t &block, block_infos_map_t &cluster_blocks,
                               block_infos_map_t &deleted_blocks, bool zero_indices) noexcept {
    if (!block) {
        return;
    }
    auto indices = block->unlink(this, true);
    if (block->is_deleted()) {
        deleted_blocks.put(block);
        cluster_blocks.remove(block);
    }
    if (zero_indices) {
        for (auto i : indices) {
            blocks[i] = nullptr;
        }
    }
}

void file_info_t::remove_blocks() noexcept {
    auto &cluster = *folder_info->get_folder()->get_cluster();
    auto &blocks_map = cluster.get_blocks();
    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    for (auto &it : blocks) {
        remove_block(it, blocks_map, deleted_blocks_map);
    }
    if (blocks.size()) {
        mark_dirty();
    }
    blocks.clear();
}

void file_info_t::append_block(const model::block_info_ptr_t &block, size_t index) noexcept {
    assert(index < blocks.size() && "blocks should be reserve enough space");
    blocks[index] = block;
    block->link(this, index);
    mark_dirty();
}

std::uint64_t file_info_t::get_block_offset(size_t block_index) const noexcept {
    assert(!blocks.empty());
    return block_size * block_index;
}

void file_info_t::mark_local_available(size_t block_index) noexcept {
    blocks[block_index]->mark_local_available(this);
    mark_dirty();
}

const boost::filesystem::path &file_info_t::get_path() noexcept {
    if (!path) {
        path = folder_info->get_folder()->get_path() / std::string(get_name());
    }
    return path.value();
}

void file_info_t::record_update(const device_t &source) noexcept {
    uint64_t value = version.counters_size() + 1;
    auto &device_id = source.device_id.get_sha256();
    uint64_t id;
    std::copy(device_id.data(), device_id.data() + sizeof(id), reinterpret_cast<char *>(&id));

    auto counter = version.add_counters();
    counter->set_id(id);
    counter->set_value(value);
}

bool file_info_t::is_older(const file_info_t &other) noexcept { return sequence < other.sequence; }

file_info_ptr_t file_info_t::link(const device_ptr_t &target) noexcept {
    auto fi = folder_info->get_folder()->get_folder_info(target);
    assert(fi);

    auto &full_name = get_full_name();
    auto local_folder_info = fi->get_folder()->get_folder_info(target);
    auto &local_file_infos = local_folder_info->get_file_infos();
    auto local_file = local_file_infos.by_id(full_name);
    if (local_file) {
        assert(local_file->get_sequence() <= sequence);
        /* is being synced */
        bool match =
            (local_file->get_sequence() == sequence) && (local_file->size == size) && local_file->is_incomplete();
        if (match) {
            local_file->locked = true;
            auto &cluster = *folder_info->get_folder()->get_cluster();
            auto &blocks_map = cluster.get_blocks();
            auto &deleted_blocks_map = cluster.get_deleted_blocks();
            auto &local_blocks = local_file->get_blocks();
            size_t j = 0;
            for (; j < blocks.size() && j < local_blocks.size(); ++j) {
                auto &lb = local_blocks[j];
                if (!lb || (*lb != *blocks[j])) {
                    break;
                }
            }
            for (; j < local_blocks.size(); ++j) {
                local_file->remove_block(local_blocks[j], blocks_map, deleted_blocks_map);
            }
            local_blocks.resize(blocks.size());
            return local_file;
        }
        local_file_infos.remove(local_file);
    }
    auto db = serialize(false);
    local_file = new file_info_t(db, local_folder_info.get());
    local_file_infos.put(local_file);
    local_file->mark_dirty();
    local_file->locked = true;
    local_file->blocks.resize(blocks.size());
    if (blocks.size()) {
        local_file->mark_incomplete();
    }
    return local_file;
}

void file_info_t::after_sync() noexcept {
    assert(locked);
    locked = false;
    if (sequence > folder_info->get_max_sequence()) {
        folder_info->set_max_sequence(sequence);
    }
}

void file_info_t::unlock() noexcept { locked = false; }

void file_info_t::lock() noexcept { locked = true; }

proto::FileInfo file_info_t::get() const noexcept {
    proto::FileInfo r;
    r.set_name(std::string(get_name()));
    r.set_type(type);
    r.set_size(size);
    r.set_permissions(permissions);
    r.set_modified_s(modified_s);
    r.set_modified_ns(modified_ns);
    r.set_modified_by(modified_by);
    r.set_deleted(deleted);
    r.set_invalid(invalid);
    r.set_permissions(permissions);

    auto v = r.mutable_version();
    for (size_t i = 0; i < version.counters_size(); ++i) {
        *v->add_counters() = version.counters(i);
    }
    r.set_sequence(sequence);
    r.set_size(size);

    auto r_blocks = r.mutable_blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto tb = r_blocks->Add();
        auto &sb = blocks[i];
        tb->set_offset(get_block_offset(i));
        tb->set_size(sb->get_size());
        tb->set_hash(sb->get_hash());
        tb->set_weak_hash(sb->get_weak_hash());
    }

    r.set_symlink_target(symlink_target);

    return r;
}

} // namespace syncspirit::model
