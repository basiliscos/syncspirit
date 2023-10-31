// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "discovery_support.h"
#include "utils/beast_support.h"
#include "utils/error_code.h"
#include <boost/beast/http.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <nlohmann/json.hpp>
#include <charconv>
#include <sstream>
#include <iomanip>
#include <cctype>

using namespace syncspirit::utils;

namespace syncspirit::proto {

namespace http = boost::beast::http;
using json = nlohmann::json;

outcome::result<URI> make_announce_request(fmt::memory_buffer &buff, const URI &announce_uri,
                                           const utils::uri_container_t &listening_uris) noexcept {
    json payload = json::object();
    json addresses = json::array();
    for (auto &uri : listening_uris) {
        addresses.push_back(uri.full);
    }
    payload["addresses"] = addresses;

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
    return uri;
}

outcome::result<URI> make_discovery_request(fmt::memory_buffer &buff, const URI &announce_uri,
                                            const model::device_id_t device_id) noexcept {
    auto target = fmt::format("?device={}", device_id.get_value());
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
    return uri;
}

outcome::result<std::uint32_t> parse_announce(http::response<http::string_body> &res) noexcept {
    auto code = res.result_int();
    if (code != 204 && code != 429) {
        auto ec = make_error_code(error_code_t::unexpected_response_code);
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
        auto ec = make_error_code(error_code_t::negative_reannounce_interval);
        return ec;
    }
    return static_cast<std::uint32_t>(reannounce);
}

outcome::result<utils::uri_container_t> parse_contact(http::response<http::string_body> &res) noexcept {
    auto code = res.result_int();
    if (code == 404) {
        return utils::uri_container_t{};
    };
    if (code != 200) {
        return make_error_code(error_code_t::unexpected_response_code);
    };

    auto &body = res.body();
    auto ptr = body.data();
    auto data = json::parse(ptr, ptr + body.size(), nullptr, false);
    if (data.is_discarded()) {
        return make_error_code(error_code_t::malformed_json);
    }
    if (!data.is_object()) {
        return make_error_code(error_code_t::incorrect_json);
    }

    auto &addresses = data["addresses"];
    if (!addresses.is_array()) {
        return make_error_code(error_code_t::incorrect_json);
    }

    uri_container_t urls;
    for (auto &it : addresses) {
        if (!it.is_string()) {
            return make_error_code(error_code_t::incorrect_json);
        }
        auto uri_str = it.get<std::string>();
        auto uri_option = utils::parse(uri_str.c_str());
        if (!uri_option) {
            continue;
        }
        urls.emplace_back(std::move(uri_option.value()));
    }
    if (urls.empty()) {
        return make_error_code(error_code_t::malformed_url);
    }

    auto &seen = data["seen"];
    if (!seen.is_string()) {
        return make_error_code(error_code_t::incorrect_json);
    }
    try {
        auto date_str = seen.get<std::string>();
        auto valid_count = date_str.size();
        while (valid_count > 0 && !isdigit(date_str[valid_count - 1])) {
            --valid_count;
        }
        auto date_substr = std::string(date_str.data(), valid_count);
        auto date = boost::posix_time::from_iso_extended_string(date_substr);
        (void)date;
        return outcome::success(std::move(urls));
    } catch (...) {
        return make_error_code(error_code_t::malformed_date);
    }
}

} // namespace syncspirit::proto
