// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "beast_support.h"
#include <boost/beast/version.hpp>

namespace asio = boost::asio;

namespace syncspirit::utils {

template <typename Request> void set_ua(Request &req) noexcept {
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
}

outcome::result<void> serialize(http::request<http::empty_body> &req, bytes_t &buff) {

    set_ua(req);
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
    }
    return outcome::success();
}

outcome::result<void> serialize(http::request<http::string_body> &req, bytes_t &buff) {
    set_ua(req);

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

} // namespace syncspirit::utils
