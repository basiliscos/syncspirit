#include "error_code.h"

namespace syncspirit::utils::detail {

const char *error_code_category::name() const noexcept { return "syncspirit_error"; }

std::string error_code_category::message(int c) const {
    switch (static_cast<error_code>(c)) {
    case error_code::success:
        return "success";
    case error_code::no_location:
        return "no location";
    case error_code::incomplete_discovery_reply:
        return "incomplete discovery reply";
    case error_code::no_st:
        return "no st (search target)";
    case error_code::no_usn:
        return "no usn";
    case error_code::igd_mismatch:
        return "IGD (InternetGatewayDevice) mismatch";
    default:
        return "unknown";
    }
}

} // namespace syncspirit::utils::detail

namespace syncspirit::utils {

const static detail::error_code_category category;
const detail::error_code_category &error_code_category() { return category; }

} // namespace syncspirit::utils
