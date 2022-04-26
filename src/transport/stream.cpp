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

stream_sp_t initiate_tls_passive(ra::supervisor_asio_t &sup, const utils::key_pair_t &my_keys,
                                 tcp::socket peer_sock) noexcept {
    ssl_junction_t ssl{{}, &my_keys, false, ""};
    transport_config_t cfg{ssl_option_t(ssl), {}, sup, std::move(peer_sock)};
    return new steam_impl_t<ssl_socket_t>(cfg);
}

stream_sp_t initiate_tls_active(ra::supervisor_asio_t &sup, const utils::key_pair_t &my_keys,
                                const model::device_id_t &expected_peer, const utils::URI &uri, bool sni,
                                std::string_view alpn) noexcept {
    ssl_junction_t ssl{expected_peer, &my_keys, sni, alpn};
    transport_config_t cfg{ssl_option_t(ssl), uri, sup, {}};
    return new steam_impl_t<ssl_socket_t>(cfg);
}

stream_sp_t initiate_stream(transport_config_t &config) noexcept {
    assert(config.uri.proto == "tcp");
    if (config.ssl_junction) {
        using socket_t = ssl_socket_t;
        return new steam_impl_t<socket_t>(config);
    } else {
        using socket_t = tcp_socket_t;
        return new steam_impl_t<socket_t>(config);
    }
}

} // namespace syncspirit::transport
