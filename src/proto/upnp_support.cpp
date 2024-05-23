// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include <boost/beast/http.hpp>
#include <pugixml.hpp>
#include <spdlog/spdlog.h>
#include "upnp_support.h"
#include "utils/error_code.h"
#include "utils/beast_support.h"

using namespace syncspirit::utils;
using namespace syncspirit::model;

namespace syncspirit::proto {

const char *upnp_fields::st = "ST";
const char *upnp_fields::man = "MAN";
const char *upnp_fields::mx = "MX";
const char *upnp_fields::usn = "USN";

const char *upnp_addr = "239.255.255.250";

static const char *igd_v1_st_v = "urn:schemas-upnp-org:device:InternetGatewayDevice:1";
static const char *igd_man_v = "\"ssdp:discover\"";
static const char *igd_wan_xpath = "//service[../../deviceType = 'urn:schemas-upnp-org:device:WANConnectionDevice:1' "
                                   "and serviceType = 'urn:schemas-upnp-org:service:WANIPConnection:1']";
static const char *igd_wan_service = "urn:schemas-upnp-org:service:WANIPConnection:1";
static const char *soap_GetExternalIPAddress = "GetExternalIPAddress";
static const char *soap_AddPortMapping = "AddPortMapping";
static const char *soap_DeletePortMapping = "DeletePortMapping";
static const char *soap_GetSpecificPortMappingEntry = "GetSpecificPortMappingEntry";
static const char *external_ip_xpath = "//NewExternalIPAddress";
static const char *port_mapping_success_xpath = "//*[local-name() = 'AddPortMappingResponse']";
static const char *port_unmapping_success_xpath = "//*[local-name() = 'DeletePortMappingResponse']";
static const char *port_mapping_validation_success_xpath = "//*[local-name() = 'GetSpecificPortMappingEntryResponse']";

constexpr unsigned http_version = 11;

namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace sys = boost::system;

outcome::result<void> make_discovery_request(fmt::memory_buffer &buff, std::uint32_t max_wait) noexcept {
    std::string upnp_host = fmt::format("{}:{}", upnp_addr, upnp_port);
    std::string upnp_max_wait = fmt::format("{}", max_wait);

    auto req = http::request<http::empty_body>();
    req.version(http_version);
    req.method(http::verb::msearch);
    req.target("*");
    req.set(http::field::host, upnp_host.data());
    req.set(upnp_fields::st, igd_v1_st_v);
    req.set(upnp_fields::man, igd_man_v);
    req.set(upnp_fields::mx, upnp_max_wait.data());

    return serialize(req, buff);
}

outcome::result<discovery_result> parse(const char *data, std::size_t bytes) noexcept {
    http::parser<false, http::empty_body> parser;
    auto buff = asio::const_buffers_1(data, bytes);
    sys::error_code ec;
    parser.put(buff, ec);
    if (ec) {
        return ec;
    }

    parser.put_eof(ec);
    if (ec) {
        return ec;
    }

    if (!parser.is_done()) {
        return error_code_t::incomplete_discovery_reply;
    }

    auto &message = parser.get();
    auto it_location = message.find(http::field::location);
    if (it_location == message.end()) {
        return error_code_t::no_location;
    }
    auto location_option = utils::parse(it_location->value());

    auto it_st = message.find(upnp_fields::st);
    if (it_st == message.end()) {
        return error_code_t::no_st;
    }

    auto it_usn = message.find(upnp_fields::usn);
    if (it_usn == message.end()) {
        return error_code_t::no_usn;
    }
    auto st = it_st->value();
    if (st != igd_v1_st_v) {
        std::string v((const char *)st.data(), st.size());
        spdlog::warn("upnp_support, igd version {}", v);
        return error_code_t::igd_mismatch;
    }

    auto usn = it_usn->value();
    return discovery_result{
        std::move(location_option),
        std::string(st.begin(), st.end()),
        std::string(usn.begin(), usn.end()),
    };
}

outcome::result<void> make_description_request(fmt::memory_buffer &buff, const uri_ptr_t &uri) noexcept {
    http::request<http::empty_body> req;
    req.method(http::verb::get);
    req.version(http_version);
    req.target(uri->encoded_path());
    req.set(http::field::host, uri->host());
    req.set(http::field::connection, "close");

    return serialize(req, buff);
}

outcome::result<igd_result> parse_igd(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code_t::xml_parse_error;
    }
    auto node = doc.select_node(igd_wan_xpath);
    if (!node) {
        return error_code_t::wan_notfound;
    }

    auto control_url = node.node().child_value("controlURL");
    auto description_url = node.node().child_value("SCPDURL");
    if (control_url && description_url) {
        return igd_result{control_url, description_url};
    }
    return error_code_t::wan_notfound;
}

outcome::result<void> make_external_ip_request(fmt::memory_buffer &buff, const uri_ptr_t &uri) noexcept {
    http::request<http::string_body> req;
    std::string soap_action = fmt::format("\"{0}#{1}\"", igd_wan_service, soap_GetExternalIPAddress);
    req.method(http::verb::post);
    req.version(http_version);
    req.target(uri->encoded_path());
    req.set(http::field::connection, "close");
    req.set(http::field::host, uri->host());
    req.set(http::field::soapaction, soap_action);
    req.set(http::field::pragma, "no-cache");
    req.set(http::field::cache_control, "no-cache");
    req.set(http::field::content_type, "text/xml");

    std::string body = fmt::format("<?xml version='1.0'?>"
                                   "<s:Envelope xmlns:s='http://schemas.xmlsoap.org/soap/envelope/' "
                                   "s:encodingStyle='http://schemas.xmlsoap.org/soap/encoding/'>"
                                   "<s:Body><u:{0} xmlns:u='{1}'></u:{0}></s:Body></s:Envelope>",
                                   soap_GetExternalIPAddress, igd_wan_service);
    req.body() = body;
    req.prepare_payload();
    return serialize(req, buff);
}

outcome::result<std::string> parse_external_ip(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code_t::xml_parse_error;
    }
    auto node = doc.select_node(external_ip_xpath);
    if (!node) {
        return error_code_t::xml_parse_error;
    }

    return node.node().child_value();
}

outcome::result<void> make_mapping_request(fmt::memory_buffer &buff, const uri_ptr_t &uri, std::uint16_t external_port,
                                           const std::string &internal_ip, std::uint16_t internal_port) noexcept {
    http::request<http::string_body> req;
    std::string soap_action = fmt::format("\"{0}#{1}\"", igd_wan_service, soap_AddPortMapping);
    req.method(http::verb::post);
    req.version(http_version);
    req.target(uri->encoded_path());
    req.set(http::field::host, uri->host());
    req.set(http::field::connection, "close");
    req.set(http::field::soapaction, soap_action);
    req.set(http::field::pragma, "no-cache");
    req.set(http::field::cache_control, "no-cache");
    req.set(http::field::content_type, "text/xml");

    std::string body = fmt::format("<?xml version='1.0'?>"
                                   "<s:Envelope xmlns:s='http://schemas.xmlsoap.org/soap/envelope/' "
                                   "s:encodingStyle='http://schemas.xmlsoap.org/soap/encoding/'>"
                                   "<s:Body><u:{0} xmlns:u='{1}'>"
                                   "<NewRemoteHost></NewRemoteHost>"
                                   "<NewExternalPort>{2}</NewExternalPort>"
                                   "<NewProtocol>TCP</NewProtocol>"
                                   "<NewInternalPort>{3}</NewInternalPort>"
                                   "<NewInternalClient>{4}</NewInternalClient>"
                                   "<NewEnabled>1</NewEnabled>"
                                   "<NewPortMappingDescription>syncspirit</NewPortMappingDescription>"
                                   "<NewLeaseDuration>0</NewLeaseDuration>"
                                   "</u:{0}></s:Body></s:Envelope>",
                                   soap_AddPortMapping, igd_wan_service, external_port, internal_port, internal_ip);
    req.body() = body;
    req.prepare_payload();

    return serialize(req, buff);
}

outcome::result<void> make_unmapping_request(fmt::memory_buffer &buff, const uri_ptr_t &uri,
                                             std::uint16_t external_port) noexcept {
    http::request<http::string_body> req;
    std::string soap_action = fmt::format("\"{0}#{1}\"", igd_wan_service, soap_DeletePortMapping);
    req.method(http::verb::post);
    req.version(http_version);
    req.target(uri->encoded_path());
    req.set(http::field::host, uri->host());
    req.set(http::field::connection, "close");
    req.set(http::field::soapaction, soap_action);
    req.set(http::field::pragma, "no-cache");
    req.set(http::field::cache_control, "no-cache");
    req.set(http::field::content_type, "text/xml");

    std::string body = fmt::format("<?xml version='1.0'?>"
                                   "<s:Envelope xmlns:s='http://schemas.xmlsoap.org/soap/envelope/' "
                                   "s:encodingStyle='http://schemas.xmlsoap.org/soap/encoding/'>"
                                   "<s:Body><u:{0} xmlns:u='{1}'>"
                                   "<NewRemoteHost></NewRemoteHost>"
                                   "<NewExternalPort>{2}</NewExternalPort>"
                                   "<NewProtocol>TCP</NewProtocol>"
                                   "</u:{0}></s:Body></s:Envelope>",
                                   soap_DeletePortMapping, igd_wan_service, external_port);
    req.body() = body;
    req.prepare_payload();

    return serialize(req, buff);
}

outcome::result<void> make_mapping_validation_request(fmt::memory_buffer &buff, const uri_ptr_t &uri,
                                                      std::uint16_t external_port) noexcept {
    http::request<http::string_body> req;
    std::string soap_action = fmt::format("\"{0}#{1}\"", igd_wan_service, soap_GetSpecificPortMappingEntry);
    req.method(http::verb::post);
    req.version(http_version);
    req.target(uri->encoded_path());
    req.set(http::field::host, uri->host());
    req.set(http::field::connection, "close");
    req.set(http::field::soapaction, soap_action);
    req.set(http::field::pragma, "no-cache");
    req.set(http::field::cache_control, "no-cache");
    req.set(http::field::content_type, "text/xml");

    std::string body = fmt::format("<?xml version='1.0'?>"
                                   "<s:Envelope xmlns:s='http://schemas.xmlsoap.org/soap/envelope/' "
                                   "s:encodingStyle='http://schemas.xmlsoap.org/soap/encoding/'>"
                                   "<s:Body><u:{0} xmlns:u='{1}'>"
                                   "<NewRemoteHost></NewRemoteHost>"
                                   "<NewExternalPort>{2}</NewExternalPort>"
                                   "<NewProtocol>TCP</NewProtocol>"
                                   "</u:{0}></s:Body></s:Envelope>",
                                   soap_GetSpecificPortMappingEntry, igd_wan_service, external_port);
    req.body() = body;
    req.prepare_payload();

    return serialize(req, buff);
}

outcome::result<bool> parse_mapping(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code_t::xml_parse_error;
    }
    auto node = doc.select_node(port_mapping_success_xpath);
    return static_cast<bool>(node);
}

outcome::result<bool> parse_unmapping(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code_t::xml_parse_error;
    }
    auto node = doc.select_node(port_unmapping_success_xpath);
    return static_cast<bool>(node);
}

outcome::result<bool> parse_mapping_validation(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code_t::xml_parse_error;
    }
    auto node = doc.select_node(port_mapping_validation_success_xpath);
    return static_cast<bool>(node);
}

} // namespace syncspirit::proto
