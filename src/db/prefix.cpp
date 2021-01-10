#include "prefix.h"

using namespace syncspirit::db;

value_t prefixer_t<prefix::misc>::make(std::string_view name) noexcept {
    std::string r;
    r.resize(name.size() + 1);
    *r.data() = (char)prefix::misc;
    std::copy(name.begin(), name.end(), r.begin() + 1);
    return r;
}

static value_t mk_folder_prefix(char prefix, const std::string_view &id) noexcept {
    std::string r;
    r.resize(id.size() + 1);
    *r.data() = prefix;
    std::copy(id.begin(), id.end(), r.begin() + 1);
    return r;
}

value_t prefixer_t<prefix::folder_info>::make(const std::string_view &id) noexcept {
    return mk_folder_prefix((char)prefix::folder_info, id);
}

value_t prefixer_t<prefix::folder_index>::make(const std::string_view &id) noexcept {
    return mk_folder_prefix((char)prefix::folder_index, id);
}

value_t prefixer_t<prefix::folder_local_device>::make(const std::string_view &id) noexcept {
    return mk_folder_prefix((char)prefix::folder_local_device, id);
}
