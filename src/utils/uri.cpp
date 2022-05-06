// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "uri.h"
#include <charconv>
#include <regex>
#include <uriparser/Uri.h>

namespace syncspirit::utils {

boost::optional<URI> parse(const char *uri) { return parse(boost::string_view(uri, strlen(uri))); }

boost::optional<URI> parse(const boost::string_view &uri) {
    using result_t = boost::optional<URI>;
    using guard_t = std::unique_ptr<UriUriA, std::function<void(UriUriA *)>>;

    UriUriA obj;
    const char *errorPos;

    if (uriParseSingleUriExA(&obj, uri.begin(), uri.end(), &errorPos) != URI_SUCCESS) {
        return result_t{};
    }
    guard_t guard(&obj, [](auto ptr) { uriFreeUriMembersA(ptr); });

    std::string proto(obj.scheme.first, obj.scheme.afterLast);
    std::string port_str = std::string(obj.portText.first, obj.portText.afterLast);
    std::uint16_t port = 0;
    if (port_str.size() > 0) {
        std::from_chars(obj.portText.first, obj.portText.afterLast, port);
    }
    if (port == 0) {
        if (proto == "http") {
            port = 80;
        } else if (proto == "https") {
            port = 443;
        }
    }
    std::string path;
    for (auto p = obj.pathHead; p; p = p->next) {
        path += std::string("/") + std::string(p->text.first, p->text.afterLast);
    }
    auto p = obj.pathHead;

    return result_t{URI{
        std::string(uri),                                        // full
        std::string(obj.hostText.first, obj.hostText.afterLast), // host
        port,                                                    // port
        proto,                                                   // proto
        port_str,                                                // service
        path,                                                    // path
        std::string(obj.query.first, obj.query.afterLast),       // query
        std::string(obj.fragment.first, obj.fragment.afterLast), // fragment
    }};
}

void URI::reconstruct() noexcept { full = proto + "://" + host + ":" + std::to_string(port) + path + query; }

void URI::set_path(const std::string &value) noexcept {
    path = "";
    if (value.size() && value[0] != '/') {
        path += "/";
    }
    path += value;

    reconstruct();
}

void URI::set_query(const std::string &value) noexcept {
    query = "";
    if (value.size() && value[0] != '?') {
        query += "?";
    }
    query += value;

    reconstruct();
}

std::string URI::relative() const noexcept { return path + query; }

auto URI::decompose_query() const noexcept -> StringPairs {
    StringPairs r;
    UriQueryListA *queryList;
    int itemCount;
    auto q = query.data();
    if (uriDissectQueryMallocA(&queryList, &itemCount, q, q + query.size()) != URI_SUCCESS) {
        return {};
    }
    using guard_t = std::unique_ptr<UriQueryListA, std::function<void(UriQueryListA *)>>;
    guard_t guard(queryList, [](auto ptr) { uriFreeQueryListA(ptr); });
    auto p = queryList;
    while (p) {
        r.emplace_back(StringPair(p->key, p->value));
        p = p->next;
    }
    return r;
}

}; // namespace syncspirit::utils
