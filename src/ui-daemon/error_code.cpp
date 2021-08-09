#include "error_code.h"

namespace syncspirit::daemon::detail {

const char *error_code_category::name() const noexcept { return "syncspirit_daemon_error"; }

std::string error_code_category::message(int c) const {
    std::string r;
    switch (static_cast<error_code_t>(c)) {
    case error_code_t::success:
        r = "success";
        break;
    case error_code_t::command_is_missing:
        r = "command is missing";
        break;
    case error_code_t::invalid_device_id:
        r = "invalid device id";
        break;
    case error_code_t::missing_device_label:
        r = "device label is missing";
        break;
    case error_code_t::missing_folder_label:
        r = "folder label is missing";
        break;
    case error_code_t::missing_folder:
        r = "folder is missing";
        break;
    case error_code_t::missing_device:
        r = "device is is missing";
        break;
    case error_code_t::missing_folder_path:
        r = "folder path is missing";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
};

} // namespace syncspirit::daemon::detail

namespace syncspirit::daemon {

const static detail::error_code_category category;

const detail::error_code_category &error_code_category() { return category; }

} // namespace syncspirit::daemon
