#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <string_view>
#include <sstream>
#include <pugixml.hpp>
#include "error_code.h"
#include "upnp_support.h"

namespace syncspirit::utils {

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
static const char *external_ip_xpath = "//NewExternalIPAddress";

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
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    auto serializer = http::serializer<true, http::empty_body>(req);
    serializer.split(false);

    sys::error_code ec;
    serializer.next(ec, [&](auto ec, const auto &buff_seq) {
        if (!ec) {
            auto sz = buffer_size(buff_seq);
            buff.resize(sz);
            buffer_copy(asio::mutable_buffer(buff.data(), sz), buff_seq);
        }
    });
    if (ec) {
        return ec;
    };
    return outcome::success();
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
        return error_code::incomplete_discovery_reply;
    }

    auto &message = parser.get();
    auto it_location = message.find(http::field::location);
    if (it_location == message.end()) {
        return error_code::no_location;
    }
    // std::string_view location_str = it_location->value();
    auto location_option = parse(it_location->value());

    auto it_st = message.find(upnp_fields::st);
    if (it_st == message.end()) {
        return error_code::no_st;
    }

    auto it_usn = message.find(upnp_fields::usn);
    if (it_usn == message.end()) {
        return error_code::no_usn;
    }

    auto st = it_st->value();
    if (st != igd_v1_st_v) {
        return error_code::igd_mismatch;
    }

    auto usn = it_usn->value();
    return discovery_result{
        *location_option,
        std::string(st.begin(), st.end()),
        std::string(usn.begin(), usn.end()),
    };
}

outcome::result<void> make_description_request(fmt::memory_buffer &buff, const discovery_result &dr) noexcept {
    auto &location = dr.location;
    http::request<http::empty_body> req;
    req.method(http::verb::get);
    req.version(http_version);
    req.target(location.path);
    req.set(http::field::host, location.host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    sys::error_code ec;
    auto serializer = http::serializer<true, http::empty_body>(req);
    serializer.next(ec, [&](auto ec, const auto &buff_seq) {
        if (!ec) {
            auto sz = buffer_size(buff_seq);
            buff.resize(sz);
            buffer_copy(asio::mutable_buffer(buff.data(), sz), buff_seq);
        }
    });
    if (ec) {
        return ec;
    };
    return outcome::success();
}

outcome::result<igd_result> parse_igd(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code::xml_parse_error;
    }
    auto node = doc.select_node(igd_wan_xpath);
    if (!node) {
        return error_code::wan_notfound;
    }

    auto control_url = node.node().child_value("controlURL");
    auto description_url = node.node().child_value("SCPDURL");
    if (control_url && description_url) {
        return igd_result{control_url, description_url};
    }
    return error_code::wan_notfound;
}

outcome::result<void> make_external_ip_request(fmt::memory_buffer &buff, const URI &uri) noexcept {
    http::request<http::string_body> req;
    std::string soap_action = fmt::format("{0}#{1}", igd_wan_service, soap_GetExternalIPAddress);
    req.method(http::verb::post);
    req.version(http_version);
    req.target(uri.path);
    req.set(http::field::host, uri.host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
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

    sys::error_code ec;
    auto serializer = http::serializer<true, http::string_body>(req);
    serializer.next(ec, [&](auto ec, const auto &buff_seq) {
        if (!ec) {
            auto sz = buffer_size(buff_seq);
            buff.resize(sz);
            buffer_copy(asio::mutable_buffer(buff.data(), sz), buff_seq);
        }
    });
    if (ec) {
        return ec;
    }
    return outcome::success();
}

outcome::result<std::string> parse_external_ip(const char *data, std::size_t bytes) noexcept {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data, bytes);
    if (!result) {
        return error_code::xml_parse_error;
    }
    auto node = doc.select_node(external_ip_xpath);
    if (!node) {
        return error_code::xml_parse_error;
    }

    return node.node().child_value();
}

} // namespace syncspirit::utils
