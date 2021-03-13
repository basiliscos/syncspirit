#pragma once

#include <boost/system/error_code.hpp>

namespace syncspirit {
namespace db {

enum class error_code {
    success = 0,
    db_version_size_mismatch,
    deserialization_falure,
    invalid_device_id,
    /*
    folder_info_not_found,
    folder_info_deserialization_failure,
    folder_local_device_not_found,
    folder_index_not_found,
    folder_index_deserialization_failure,
    unknown_local_device,
    */
};

namespace detail {

class db_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

class mbdx_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

} // namespace detail

const detail::db_code_category &db_code_category();

const detail::mbdx_code_category &mbdx_code_category();

inline boost::system::error_code make_error_code(int e) { return {static_cast<int>(e), db_code_category()}; }
inline boost::system::error_code make_error_code(error_code ec) { return {static_cast<int>(ec), mbdx_code_category()}; }

} // namespace db
} // namespace syncspirit

namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::db::detail::db_code_category> : std::true_type {
    static const bool value = true;
};

template <> struct is_error_code_enum<syncspirit::db::detail::mbdx_code_category> : std::true_type {
    static const bool value = true;
};

} // namespace system
} // namespace boost
