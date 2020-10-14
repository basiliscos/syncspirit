#include "discovery_support.h"
#include "beast_support.h"
#include "error_code.h"
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>
#include <charconv>

namespace syncspirit::utils {

namespace http = boost::beast::http;
using json = nlohmann::json;

outcome::result<URI> make_announce_request(fmt::memory_buffer &buff, const URI &announce_uri,
                                           const tcp::endpoint &endpoint) noexcept {
    json payload = json::object();
    payload["addresses"] = {fmt::format("tcp://{0}:{1}", endpoint.address().to_string(), endpoint.port())};

    utils::URI uri(announce_uri);
    uri.set_path("/v2");
    http::request<http::string_body> req;
    req.method(http::verb::post);
    req.version(11);
    req.keep_alive(true);
    req.target(uri.relative());
    req.set(http::field::host, uri.host);
    req.set(http::field::content_type, "application/json");

    req.body() = payload.dump();
    req.prepare_payload();

    auto ok = serialize(req, buff);
    if (!ok)
        return ok.error();
    return std::move(uri);
}

outcome::result<URI> make_discovery_request(fmt::memory_buffer &buff, const URI &announce_uri,
                                            const proto::device_id_t device_id) noexcept {
    auto target = fmt::format("?device={}", device_id.value);
    utils::URI uri = announce_uri;
    uri.set_query(target);

    http::request<http::empty_body> req;
    req.method(http::verb::get);
    req.version(11);
    req.keep_alive(true);
    req.target(uri.relative());
    req.set(http::field::host, uri.host);

    fmt::memory_buffer tx_buff;
    auto ok = serialize(req, buff);
    if (!ok)
        return ok.error();
    return std::move(uri);
}

outcome::result<std::uint32_t> parse_announce(http::response<http::string_body> &res) noexcept {
    auto code = res.result_int();
    if (code != 204 && code != 429) {
        auto ec = make_error_code(error_code::unexpected_response_code);
        return ec;
    }

    auto convert = [&](const auto &it) -> int {
        if (it != res.end()) {
            auto str = it->value();
            int value;
            auto result = std::from_chars(str.begin(), str.end(), value);
            if (result.ec == std::errc()) {
                return value;
            }
        }
        return 0;
    };

    auto reannounce = convert(res.find("Reannounce-After"));
    if (reannounce < 1) {
        reannounce = convert(res.find("Retry-After"));
    }
    if (reannounce < 1) {
        auto ec = make_error_code(error_code::negative_reannounce_interval);
        return ec;
    }
    return static_cast<std::uint32_t>(reannounce);
}

} // namespace syncspirit::utils
