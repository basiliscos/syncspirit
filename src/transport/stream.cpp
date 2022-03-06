// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "stream.h"
#include "impl.hpp"

namespace syncspirit::transport {

stream_base_t::~stream_base_t() {}

template <typename Sock> struct steam_impl_t : base_impl_t<Sock>, interface_t<steam_impl_t<Sock>, Sock, stream_base_t> {
    using self_t = steam_impl_t;
    using socket_t = Sock;
    using parent_t = base_impl_t<Sock>;
    using parent_t::parent_t;
};

stream_sp_t initiate_stream(transport_config_t &config) noexcept {
    auto &proto = config.uri.proto;
    if (proto == "tcp") {
        if (config.ssl_junction) {
            using socket_t = ssl_socket_t;
            return new steam_impl_t<socket_t>(config);
        } else {
            using socket_t = tcp_socket_t;
            return new steam_impl_t<socket_t>(config);
        }
    }
    return stream_sp_t();
}

} // namespace syncspirit::transport
