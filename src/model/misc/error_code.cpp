// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "error_code.h"

namespace syncspirit::model {

namespace detail {

const char *error_code_category_t::name() const noexcept { return "model error"; }

std::string error_code_category_t::message(int c) const {
    std::string r;
    switch (static_cast<error_code_t>(c)) {
    case error_code_t::success:
        r = "success";
        break;
    case error_code_t::unknown_device:
        r = "peer device is unknown";
        break;
    case error_code_t::no_such_device:
        r = "no such device";
        break;
    case error_code_t::no_such_folder:
        r = "no such folder";
        break;
    case error_code_t::invalid_block_key_length:
        r = "invalid block key length";
        break;
    case error_code_t::invalid_block_prefix:
        r = "invalid block key prefix";
        break;
    case error_code_t::block_deserialization_failure:
        r = "block deserialization failure";
        break;
    case error_code_t::invalid_device_sha256_digest:
        r = "invalid device sha256 digest";
        break;
    case error_code_t::invalid_device_prefix:
        r = "invalid device key prefix";
        break;
    case error_code_t::device_deserialization_failure:
        r = "device deserialization failure";
        break;
    case error_code_t::invalid_folder_key_length:
        r = "invalid folder key length";
        break;
    case error_code_t::invalid_folder_prefix:
        r = "invalid folder prefix";
        break;
    case error_code_t::invalid_unknown_folder_length:
        r = "invalid unknown folder prefix";
        break;
    case error_code_t::folder_deserialization_failure:
        r = "folder deserialization failure";
        break;
    case error_code_t::unknown_folder_deserialization_failure:
        r = "unknown folder deserialization failure";
        break;
    case error_code_t::file_info_deserialization_failure:
        r = "file info deserialization failure";
        break;
    case error_code_t::invalid_file_info_key_length:
        r = "invalid file info key length";
        break;
    case error_code_t::invalid_file_info_prefix:
        r = "invalid file info prefix";
        break;
    case error_code_t::invalid_folder_info_key_length:
        r = "invalid folder info key length prefix";
        break;
    case error_code_t::invalid_folder_info_prefix:
        r = "invalid folder info prefix";
        break;
    case error_code_t::folder_info_deserialization_failure:
        r = "folder info deserialization failure";
        break;
    case error_code_t::invalid_ignored_device_key_length:
        r = "invalid ignored device key length";
        break;
    case error_code_t::invalid_ignored_device_prefix:
        r = "invalid ignored device prefix";
        break;
    case error_code_t::ignored_device_deserialization_failure:
        r = "ignored device deserialization failure";
        break;
    case error_code_t::invalid_ignored_folder_prefix:
        r = "invalid ignored folder prefix";
        break;
    case error_code_t::ignored_folder_deserialization_failure:
        r = "ignored folder deserialization failure";
        break;
    case error_code_t::folder_already_exists:
        r = "folder already exists";
        break;
    case error_code_t::source_device_not_exists:
        r = "source device does not exist";
        break;
    case error_code_t::folder_does_not_exist:
        r = "folder does not exist";
        break;
    case error_code_t::device_does_not_exist:
        r = "device does not exist";
        break;
    case error_code_t::folder_is_already_shared:
        r = "folder is already shared";
        break;
    case error_code_t::folder_is_not_shared:
        r = "folder is not shared";
        break;
    case error_code_t::malformed_deviceid:
        r = "device id is malformed";
        break;
    case error_code_t::invalid_block_size:
        r = "block size is invalid (i.e. greater than file size)";
        break;
    case error_code_t::unexpected_blocks:
        r = "blocks are not expected (e.g. in deleted file)";
        break;
    default:
        r = "unknown";
        break;
    }

    r += " (";
    r += std::to_string(c);
    r += ")";

    return r;
}

} // namespace detail

const static detail::error_code_category_t category;

const detail::error_code_category_t &error_code_category() { return category; }

} // namespace syncspirit::model
