#include "folder.h"
#include "file_info.h"
#include <algorithm>

namespace syncspirit::model {

file_info_t::file_info_t(const db::FileInfo &info_, folder_t *folder_) noexcept : folder{folder_} {
    std::string dbk;
    auto &name = info_.name();
    auto fi_key = folder->get_db_key();
    dbk.resize(sizeof(fi_key) + name.size());
    char *ptr = dbk.data();
    std::copy(reinterpret_cast<char *>(&fi_key), reinterpret_cast<char *>(&fi_key) + sizeof(fi_key), ptr);
    ptr += sizeof(fi_key);
    std::copy(name.begin(), name.end(), ptr);

    db_key = std::move(dbk);
    sequence = info_.sequence();

    type = info_.type();
    size = info_.size();
    permissions = info_.permissions();
    modified_s = info_.modified_s();
    modified_ns = info_.modified_ns();
    modified_by = info_.modified_by();
    deleted = info_.deleted();
    invalid = info_.invalid();
    no_permissions = info_.no_permissions();
    version = info_.version();
    block_size = info_.block_size();
    symlink_target = info_.symlink_target();
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
    return r;
}

bool file_info_t::update(const db::FileInfo &db_info) noexcept { std::abort(); }

} // namespace syncspirit::model
