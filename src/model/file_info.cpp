#include "folder.h"
#include "file_info.h"
#include "cluster.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace syncspirit::model {

file_info_t::file_info_t(const db::FileInfo &info_, folder_t *folder_) noexcept : folder{folder_} {
    db_key = generate_db_key(info_.name(), *folder);
    fields_update(info_);
    auto &blocks_map = folder->get_cluster()->get_blocks();
    for (int i = 0; i < info_.blocks_keys_size(); ++i) {
        auto key = info_.blocks_keys(i);
        auto block = blocks_map.by_key(key);
        assert(block);
        block->link(this, i);
        blocks.emplace_back(std::move(block));
    }
}

file_info_t::file_info_t(const proto::FileInfo &info_, folder_t *folder_) noexcept : folder{folder_} {
    db_key = generate_db_key(info_.name(), *folder);
    fields_update(info_);
    update_blocks(info_);
    mark_dirty();
}

template <typename Source> void file_info_t::fields_update(const Source &s) noexcept {
    sequence = s.sequence();
    type = s.type();
    size = s.size();
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

std::string file_info_t::generate_db_key(const std::string &name, const folder_t &folder) noexcept {
    std::string dbk;
    auto fi_key = folder.get_db_key();
    dbk.resize(sizeof(fi_key) + name.size());
    char *ptr = dbk.data();
    std::copy(reinterpret_cast<char *>(&fi_key), reinterpret_cast<char *>(&fi_key) + sizeof(fi_key), ptr);
    ptr += sizeof(fi_key);
    std::copy(name.begin(), name.end(), ptr);
    return dbk;
}

file_info_t::~file_info_t() {
    for (auto &b : blocks) {
        b->unlink(this);
    }
    blocks.clear();

    // no need to clean local blocks
    local_blocks.clear();
}

std::string_view file_info_t::get_name() const noexcept {
    const char *ptr = db_key.data();
    ptr += sizeof(std::uint64_t);
    return std::string_view(ptr, db_key.size() - sizeof(std::uint64_t));
}

db::FileInfo file_info_t::serialize() noexcept {
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

    for (auto &block : blocks) {
        r.mutable_blocks_keys()->Add(block->get_db_key());
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

file_status_t file_info_t::update(const local_file_t &local_file) noexcept {
    bool matched = local_file.blocks.size() == blocks.size();
    size_t i = 0;
    for (; matched && i < local_file.blocks.size(); ++i) {
        if (i < blocks.size()) {
            auto &lb = local_file.blocks[i];
            auto &sb = blocks[i];
            if (*lb == *sb) {
                lb->link(this, i);
                lb->mark_local_available(this);
            } else {
                matched = false;
            }
        }
    }
    if (matched) {
        local_blocks = blocks;
        status = file_status_t::sync;
    } else {
        local_blocks = local_file.blocks;
        status = file_status_t::newer;
    }
    return status;
}

void file_info_t::update_blocks(const proto::FileInfo &remote_info) noexcept {
    auto &blocks_map = folder->get_cluster()->get_blocks();
    for (int i = 0; i < remote_info.blocks_size(); ++i) {
        auto &b = remote_info.blocks(i);
        auto &hash = b.hash();
        auto block = blocks_map.by_id(hash);
        if (!block) {
            block = new block_info_t(b);
            block->link(this, i);
        }
        blocks.emplace_back(std::move(block));
    }
}

block_location_t file_info_t::next_block() noexcept {
    size_t i = 0;
    for (; i < blocks.size(); ++i) {
        auto &b = blocks[i];
        if (i < local_blocks.size()) {
            auto &lb = local_blocks[i];
            if (*lb != *b) {
                return {b.get(), i};
            }
        } else {
            return {b.get(), i};
        }
    }
    return {nullptr, 0};
}

void file_info_t::clone_block(file_info_t &source, std::size_t src_block_index, std::size_t dst_block_index) noexcept {
    std::abort();
}

std::uint64_t file_info_t::get_block_offset(size_t block_index) const noexcept {
    assert(!blocks.empty());
    return block_size * block_index;
}

void file_info_t::mark_local_available(size_t block_index) noexcept {
    blocks[block_index]->mark_local_available(this);
    assert(local_blocks.size() == block_index);
    local_blocks.emplace_back(blocks[block_index]);
    if (local_blocks.size() == blocks.size()) {
        spdlog::info("{}/{} is sync", folder->label(), get_name());
        status = file_status_t::sync;
        mark_dirty();
    }
}

bfs::path file_info_t::get_path() const noexcept { return folder->get_path() / std::string(get_name()); }

std::string file_info_t::get_full_name() const noexcept {
    return fmt::format("{}/{}", get_folder()->label(), get_name());
}

} // namespace syncspirit::model
