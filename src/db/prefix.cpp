#include "prefix.h"

using namespace syncspirit::db;

static value_t mk(discr_t prefix, std::string_view name) noexcept {
    std::string r;
    r.resize(name.size() + 1);
    *r.data() = (char)prefix;
    std::copy(name.begin(), name.end(), r.begin() + 1);
    return r;
}

static value_t mk(discr_t prefix, std::uint64_t db_key) noexcept {
    std::string r;
    r.resize(sizeof(db_key) + 1);
    *r.data() = (char)prefix;
    auto ptr = reinterpret_cast<const char *>(&db_key);
    std::copy(ptr, ptr + sizeof(db_key), r.begin() + 1);
    return r;
}

value_t prefixer_t<prefix::misc>::make(std::string_view name) noexcept { return mk(prefix::misc, name); }

value_t prefixer_t<prefix::device>::make(std::uint64_t db_key) noexcept { return mk(prefix::device, db_key); }

value_t prefixer_t<prefix::folder>::make(std::uint64_t db_key) noexcept { return mk(prefix::folder, db_key); }

value_t prefixer_t<prefix::folder_info>::make(std::uint64_t db_key) noexcept { return mk(prefix::folder_info, db_key); }

value_t prefixer_t<prefix::file_info>::make(const std::string &db_key) noexcept {
    return mk(prefix::file_info, db_key);
}

value_t prefixer_t<prefix::ignored_device>::make(const std::string &db_key) noexcept {
    return mk(prefix::ignored_device, db_key);
}

value_t prefixer_t<prefix::ignored_folder>::make(const std::string &db_key) noexcept {
    return mk(prefix::ignored_folder, db_key);
}

value_t prefixer_t<prefix::block_info>::make(std::uint64_t db_key) noexcept { return mk(prefix::block_info, db_key); }
