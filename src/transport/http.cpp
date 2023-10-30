// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "utils/format.hpp"
#include "http.h"
#include "impl.hpp"

namespace syncspirit::transport {

template <typename Sock> struct http_impl_t : base_impl_t<Sock>, interface_t<http_impl_t<Sock>, Sock, http_base_t> {
    using self_t = http_impl_t;
    using socket_t = Sock;
    using base_t = base_impl_t<Sock>;
    using parent_t = base_impl_t<Sock>;
    using parent_t::parent_t;
    using rx_buff_t = boost::beast::flat_buffer;
    using response_t = http::response<http::string_body>;

    void async_read(rx_buff_t &rx_buff, response_t &response, io_fn_t &on_read,
                    error_fn_t &on_error) noexcept override {
        auto owner = curry_io<self_t>(*this, on_read, on_error);
        http::async_read(base_t::sock, rx_buff, response, [owner = std::move(owner)](auto ec, auto bytes) {
            auto &strand = owner->backend->strand;
            if (ec) {
                strand.post([ec = ec, owner = std::move(owner)]() {
                    if (ec == asio::error::operation_aborted) {
                        owner->backend->cancelling = false;
                    }
                    owner->error(ec);
                });
                return;
            }
            strand.post([owner = std::move(owner), bytes]() { owner->success(bytes); });
        });
    }
};

http_sp_t initiate_http(transport_config_t &config) noexcept {
    auto &proto = config.uri.proto;
    if (proto == "http") {
        return new http_impl_t<tcp_socket_t>(config);
    } else if (proto == "https") {
        return new http_impl_t<ssl_socket_t>(config);
    }
    return http_sp_t();
}

} // namespace syncspirit::transport
