#pragma once

#include <boost/system/error_code.hpp>

namespace syncspirit::daemon {

enum class error_code_t {
    success = 0,
    command_is_missing,
    unknown_command,
    invalid_device_id,
    missing_device_label,
    missing_folder_label,
    missing_folder_path,
};

namespace detail {

class error_code_category : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};
} // namespace detail

const detail::error_code_category &error_code_category();

inline boost::system::error_code make_error_code(error_code_t e) {
    return {static_cast<int>(e), error_code_category()};
}

} // namespace syncspirit::daemon

namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::daemon::error_code_t> : std::true_type {
    static const bool value = true;
};
} // namespace system
} // namespace boost
