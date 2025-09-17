// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <boost/system/error_code.hpp>
#include "syncspirit-export.h"

namespace syncspirit::model {

enum class error_code_t {
    success = 0,
    unknown_device,
    no_such_device,
    device_already_exists,
    no_such_folder,
    invalid_block_prefix,
    invalid_block_key_length,
    invalid_device_prefix,
    invalid_device_sha256_digest,
    device_deserialization_failure,
    invalid_folder_key_length,
    invalid_folder_prefix,
    invalid_pending_folder_length,
    folder_deserialization_failure,
    pending_folder_deserialization_failure,
    invalid_file_info_key_length,
    invalid_file_info_prefix,
    invalid_folder_info_key_length,
    invalid_folder_info_prefix,
    folder_info_deserialization_failure,
    invalid_some_device_key_length,
    invalid_some_device_prefix,
    some_device_deserialization_failure,
    invalid_ignored_folder_prefix,
    ignored_folder_deserialization_failure,
    cannot_remove_self,
    source_device_not_exists,
    folder_does_not_exist,
    folder_is_already_shared,
    malformed_deviceid,
    folder_is_not_shared,
    invalid_block_size,
    missing_version,
    mismatch_file_size,
    invalid_sequence,
    empty_folder_name,
};

namespace detail {

class SYNCSPIRIT_API error_code_category_t : public boost::system::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

} // namespace detail

SYNCSPIRIT_API const detail::error_code_category_t &error_code_category();

inline boost::system::error_code make_error_code(error_code_t e) {
    return {static_cast<int>(e), error_code_category()};
}

} // namespace syncspirit::model

namespace boost {
namespace system {

template <> struct is_error_code_enum<syncspirit::model::error_code_t> : std::true_type {
    static const bool value = true;
};

} // namespace system
} // namespace boost
