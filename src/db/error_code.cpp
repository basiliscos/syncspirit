#include "error_code.h"
#include "mdbx.h"

namespace syncspirit::db {

const static detail::db_code_category category;
const static detail::mbdx_code_category mbdx_category;

const detail::db_code_category &db_code_category() { return category; }
const detail::mbdx_code_category &mbdx_code_category() { return mbdx_category; }

namespace detail {

const char *db_code_category::name() const noexcept { return "syncspirit_db_error"; }

std::string db_code_category::message(int c) const { return mdbx_strerror(c); }

const char *mbdx_code_category::name() const noexcept { return "syncspirit_mbdx_error"; }

std::string mbdx_code_category::message(int c) const {
    std::string r;
    switch (static_cast<error_code>(c)) {
    case error_code::success:
        r = "success";
        break;
    case error_code::db_version_size_mismatch:
        r = "db version size mismatch";
        break;
    case error_code::folder_info_not_found:
        r = "folder info not found in DB";
        break;
    case error_code::folder_info_deserialization_failure_t:
        r = "cannot deserialize folder info";
        break;
    case error_code::folder_local_device_not_found:
        r = "folder to local device mapping not found in DB";
        break;
    case error_code::unknown_local_device:
        r = "uknown local device";
        break;
    case error_code::folder_index_not_found:
        r = "folder index not found";
        break;
    case error_code::folder_index_deserialization_failure_t:
        r = "cannot deserialize folder index";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
}

} // namespace detail
} // namespace syncspirit::db
