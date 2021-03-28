#include "error_code.h"

namespace syncspirit::utils::detail {

const char *error_code_category::name() const noexcept { return "syncspirit_error"; }

const char *bep_error_code_category::name() const noexcept { return "syncspirit_bep_error"; }

const char *protocol_error_code_category::name() const noexcept { return "syncspirit_proto_error"; }

std::string error_code_category::message(int c) const {
    std::string r;
    switch (static_cast<error_code_t>(c)) {
    case error_code_t::success:
        r = "success";
        break;
    case error_code_t::no_location:
        r = "no location";
        break;
    case error_code_t::incomplete_discovery_reply:
        r = "incomplete discovery reply";
        break;
    case error_code_t::no_st:
        r = "no st (search target)";
        break;
    case error_code_t::no_usn:
        r = "no usn";
        break;
    case error_code_t::igd_mismatch:
        r = "IGD (InternetGatewayDevice) mismatch";
        break;
    case error_code_t::xml_parse_error:
        r = "Error parsing xml";
        break;
    case error_code_t::wan_notfound:
        r = "WAN device description was not found in the XML";
        break;
    case error_code_t::timed_out:
        r = "timeout occured";
        break;
    case error_code_t::service_not_available:
        r = "service not available";
        break;
    case error_code_t::unexpected_response_code:
        r = "unexpected response code";
        break;
    case error_code_t::negative_reannounce_interval:
        r = "negative reannounce interval";
        break;
    case error_code_t::malformed_json:
        r = "malformed json";
        break;
    case error_code_t::incorrect_json:
        r = "incorrect json";
        break;
    case error_code_t::malformed_url:
        r = "malformed url";
        break;
    case error_code_t::malformed_date:
        r = "malformed date";
        break;
    case error_code_t::transport_not_available:
        r = "transport is not available";
        break;
    case error_code_t::rx_timeout:
        r = "rx timeout";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
}

std::string bep_error_code_category::message(int c) const {
    std::string r;
    switch (static_cast<bep_error_code_t>(c)) {
    case bep_error_code_t::success:
        r = "success";
        break;
    case bep_error_code_t::protobuf_err:
        r = "error parsing protobuf message";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
}

std::string protocol_error_code_category::message(int c) const {
    std::string r;
    switch (static_cast<protocol_error_code_t>(c)) {
    case protocol_error_code_t::success:
        r = "success";
        break;
    case protocol_error_code_t::unknown_folder:
        r = "unknown folder";
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
const static detail::bep_error_code_category bep_category;
const static detail::protocol_error_code_category protocol_category;

const detail::error_code_category &error_code_category() { return category; }
const detail::bep_error_code_category &bep_error_code_category() { return bep_category; }
const detail::protocol_error_code_category &protocol_error_code_category() { return protocol_category; }

} // namespace syncspirit::utils
