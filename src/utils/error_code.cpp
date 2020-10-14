#include "error_code.h"

namespace syncspirit::utils::detail {

const char *error_code_category::name() const noexcept { return "syncspirit_error"; }

std::string error_code_category::message(int c) const {
    std::string r;
    switch (static_cast<error_code>(c)) {
    case error_code::success:
        r = "success";
        break;
    case error_code::no_location:
        r = "no location";
        break;
    case error_code::incomplete_discovery_reply:
        r = "incomplete discovery reply";
        break;
    case error_code::no_st:
        r = "no st (search target)";
        break;
    case error_code::no_usn:
        r = "no usn";
        break;
    case error_code::igd_mismatch:
        r = "IGD (InternetGatewayDevice) mismatch";
        break;
    case error_code::xml_parse_error:
        r = "Error parsing xml";
        break;
    case error_code::wan_notfound:
        r = "WAN device description was not found in the XML";
        break;
    case error_code::timed_out:
        r = "timeout occured";
        break;
    case error_code::service_not_available:
        r = "service not available";
        break;
    case error_code::unexpected_response_code:
        r = "unexpected response code";
        break;
    case error_code::negative_reannounce_interval:
        r = "negative reannounce interval";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
}

} // namespace syncspirit::utils::detail

namespace syncspirit::utils {

const static detail::error_code_category category;
const detail::error_code_category &error_code_category() { return category; }

} // namespace syncspirit::utils
