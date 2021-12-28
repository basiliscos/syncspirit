#pragma once

#include <system_error>
#include <boost/system/error_code.hpp>

namespace syncspirit::model {

enum class error_code_t {
    success = 0,
    no_such_device,
    no_such_folder,
    invalid_block_prefix,
    invalid_block_key_length,
    block_deserialization_failure,
    invalid_device_prefix,
    invalid_device_sha256_digest,
    device_deserialization_failure,
    invalid_folder_key_length,
    invalid_folder_prefix,
    folder_deserialization_failure,
    file_info_deserialization_failure,
    invalid_file_info_key_length,
    invalid_file_info_prefix,
    invalid_folder_info_key_length,
    invalid_folder_info_prefix,
    folder_info_deserialization_failure,
    invalid_ignored_device_key_length,
    invalid_ignored_device_prefix,
    ignored_device_deserialization_failure,
    invalid_ignored_folder_prefix,
    ignored_folder_deserialization_failure,
    folder_already_exists,
    source_device_not_exists,
    folder_does_not_exist,
    device_does_not_exist,
    folder_is_already_shared,
    malformed_deviceid,
    folder_is_not_shared,
    invalid_block_size,
    no_progress,
};

namespace detail {

class error_code_category_t : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

}

const detail::error_code_category_t &error_code_category();

inline boost::system::error_code make_error_code(error_code_t e) {
    return {static_cast<int>(e), error_code_category()};
}

}



namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::model::error_code_t> : std::true_type {
    static const bool value = true;
};

}
}

