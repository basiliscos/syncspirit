#include "folder_info.h"
#include "file_info.h"
#include <algorithm>

namespace syncspirit::model {

file_info_t::file_info_t(const db::FileInfo &info_, folder_info_t *folder_info_) noexcept : folder_info{folder_info_} {
    std::string dbk;
    auto &name = info_.name();
    auto fi_key = folder_info->get_db_key();
    dbk.resize(sizeof(fi_key) + name.size());
    char *ptr = dbk.data();
    std::copy(reinterpret_cast<char *>(&fi_key), reinterpret_cast<char *>(&fi_key) + sizeof(fi_key), ptr);
    ptr += sizeof(fi_key);
    std::copy(name.begin(), name.end(), ptr);

    db_key = std::move(dbk);
    sequence = info_.sequence();
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
    return r;
}

} // namespace syncspirit::model
