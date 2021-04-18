#include "folder.h"
#include "file_info.h"
#include "cluster.h"
#include <algorithm>

namespace syncspirit::model {

file_info_t::file_info_t(const db::FileInfo &info_, folder_t *folder_) noexcept : folder{folder_} {
    db_key = generate_db_key(info_.name(), *folder);
    fields_update(info_);
    auto &blocks_map = folder->get_cluster()->get_blocks();
    for (int i = 0; i < info_.blocks_keys_size(); ++i) {
        auto key = info_.blocks_keys(i);
        auto block = blocks_map.by_key(key);
        assert(block);
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

file_info_t::~file_info_t() {}

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

} // namespace syncspirit::model
