// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "stream.h"
#include "impl.hpp"

namespace syncspirit::transport {

template <typename Sock, typename Interface>
struct generic_steam_impl_t : base_impl_t<Sock>, interface_t<generic_steam_impl_t<Sock, Interface>, Sock, Interface> {
    using self_t = generic_steam_impl_t;
    using socket_t = Sock;
    using parent_t = base_impl_t<Sock>;
    using parent_t::parent_t;
};

using ssl_stream_impl_t = generic_steam_impl_t<ssl_socket_t, stream_base_t>;

struct tcp_stream_impl_t final : generic_steam_impl_t<tcp_socket_t, upgradeable_stream_base_t> {
    using parent_t = generic_steam_impl_t;

    tcp_stream_impl_t(transport_config_t &config) noexcept : parent_t(config), uri(config.uri) {}

    using parent_t::parent_t;

    stream_sp_t upgrade(ssl_junction_t &ssl, bool active_role) noexcept {
        transport_config_t cfg{ssl_option_t(ssl), uri, supervisor, std::move(sock), active_role};
        return new ssl_stream_impl_t(cfg);
    }

    utils::URI uri;
};

stream_sp_t initiate_tls_passive(ra::supervisor_asio_t &sup, const utils::key_pair_t &my_keys,
                                 tcp::socket peer_sock) noexcept {
    ssl_junction_t ssl{{}, &my_keys, false, ""};
    transport_config_t cfg{ssl_option_t(ssl), {}, sup, std::move(peer_sock), false};
    return new ssl_stream_impl_t(cfg);
}

stream_sp_t initiate_tls_active(ra::supervisor_asio_t &sup, const utils::key_pair_t &my_keys,
                                const model::device_id_t &expected_peer, const utils::URI &uri, bool sni,
                                std::string_view alpn) noexcept {
    ssl_junction_t ssl{expected_peer, &my_keys, sni, alpn};
    transport_config_t cfg{ssl_option_t(ssl), uri, sup, {}, true};
    return new ssl_stream_impl_t(cfg);
}

stream_sp_t initiate_stream(transport_config_t &config) noexcept {
    assert(config.uri.proto == "tcp");
    if (config.ssl_junction) {
        return new ssl_stream_impl_t(config);
    } else {
        return new tcp_stream_impl_t(config);
    }
}

} // namespace syncspirit::transport
