// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "error_code.h"
#include <map>

namespace syncspirit::utils::detail {

const char *error_code_category::name() const noexcept { return "syncspirit_error"; }

const char *bep_error_code_category::name() const noexcept { return "syncspirit_bep_error"; }

const char *protocol_error_code_category::name() const noexcept { return "syncspirit_proto_error"; }

const char *request_error_code_category::name() const noexcept { return "syncspirit_request_error"; }

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
        r = "timeout occurred";
        break;
    case error_code_t::service_not_available:
        r = "service not available";
        break;
    case error_code_t::cant_determine_config_dir:
        r = "config dir cannot be determined";
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
    case error_code_t::tx_timeout:
        r = "tx timeout";
        break;
    case error_code_t::announce_failed:
        r = "announce failed";
        break;
    case error_code_t::discovery_failed:
        r = "discovery failed";
        break;
    case error_code_t::endpoint_failed:
        r = "endpoint failed";
        break;
    case error_code_t::portmapping_failed:
        r = "port mapping failed";
        break;
    case error_code_t::unknown_sink:
        r = "unknown sink";
        break;
    case error_code_t::misconfigured_default_logger:
        r = "default logger is missing or has no sinks";
        break;
    case error_code_t::already_shared:
        r = "folder is already shared with the peer";
        break;
    case error_code_t::connection_impossible:
        r = "cannot establish connection to the peer";
        break;
    case error_code_t::already_connected:
        r = "peer is already connected";
        break;
    case error_code_t::cannot_get_public_relays:
        r = "cannot get public relays";
        break;
    case error_code_t::protocol_error:
        r = "protocol error";
        break;
    case error_code_t::relay_failure:
        r = "relay failure";
        break;
    case error_code_t::invalid_deviceid:
        r = "invalid device id";
        break;
    case error_code_t::cares_failure:
        r = "cares failure";
        break;
    case error_code_t::peer_has_been_removed:
        r = "peer has been removed";
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
    case bep_error_code_t::unexpected_message:
        r = "unexpected message";
        break;
    case bep_error_code_t::unexpected_response:
        r = "response without request";
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
    case protocol_error_code_t::digest_mismatch:
        r = "digest mismatch";
        break;
    default:
        r = "unknown";
    }
    r += " (";
    r += std::to_string(c) + ")";
    return r;
}

std::string request_error_code_category::message(int c) const {
    std::string r;
    switch (static_cast<request_error_code_t>(c)) {
    case request_error_code_t::success:
        r = "success";
        break;
    case request_error_code_t::generic:
        r = "generic error";
        break;
    case request_error_code_t::no_such_file:
        r = "no such a file";
        break;
    case request_error_code_t::invalid_file:
        r = "invalid file";
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
const static detail::request_error_code_category request_category;

const detail::error_code_category &error_code_category() { return category; }
const detail::bep_error_code_category &bep_error_code_category() { return bep_category; }
const detail::protocol_error_code_category &protocol_error_code_category() { return protocol_category; }
const detail::request_error_code_category &request_error_code_category() { return request_category; }

boost::system::error_code adapt(const std::error_code &ec) noexcept {
    struct category_adapter_t : public boost::system::error_category {
        category_adapter_t(const std::error_category &category) : m_category(category) {}

        const char *name() const noexcept { return m_category.name(); }

        std::string message(int ev) const { return m_category.message(ev); }

      private:
        const std::error_category &m_category;
    };

    using map_t = std::map<std::string, category_adapter_t>;
    static thread_local map_t name2cat;
    auto result = name2cat.emplace(ec.category().name(), ec.category());
    auto &category = result.first->second;
    return boost::system::error_code(ec.value(), category);
}

} // namespace syncspirit::utils
