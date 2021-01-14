#include "peer_supervisor.h"
#include "peer_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

template <class> inline constexpr bool always_false_v = false;

peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg)
    : parent_t{cfg}, device_name{cfg.device_name}, ssl_pair{*cfg.ssl_pair}, bep_config(cfg.bep_config) {}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::peers, get_address());
        p.discover_name(names::coordinator, coordinator, true).link(false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&peer_supervisor_t::on_connect_request);
        p.subscribe_actor(&peer_supervisor_t::on_connect_notify);
    });
}

std::string peer_supervisor_t::get_peer_identity(const actor_base_t &actor) const noexcept {
    auto &peer_addr = actor.get_address();
    auto it_id = addr2id.find(peer_addr);
    if (it_id != addr2id.end()) {
        return std::string(it_id->second.get_short());
    }
    auto req = addr2req.at(peer_addr);
    auto &payload = req->payload.request_payload->payload;
    auto peer_identity = std::visit(
        [&](auto &&arg) mutable -> std::string {
            using P = payload::connect_request_t;
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, P::connect_info_t>) {
                auto &peer_id = arg.device_id;
                return std::string(peer_id.get_short());
            } else if constexpr (std::is_same_v<T, P::connected_info_t>) {
                std::stringstream ss;
                ss << arg.remote;
                return ss.str();
            } else {
                static_assert(always_false_v<T>, "non-exhaustive visitor!");
            }
        },
        payload);
    return peer_identity;
}

void peer_supervisor_t::on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept {
    auto &peer_addr = actor->get_address();
    auto identity = get_peer_identity(*actor);
    spdlog::trace("peer_supervisor_t::on_child_shutdown, peer: {}, reason: {}", identity, ec.message());
    auto it_req = addr2req.find(peer_addr);
    if (it_req != addr2req.end()) {
        reply_with_error(*it_req->second, utils::make_error_code(utils::error_code::cannot_connect_to_peer));
    } else {
        send<payload::disconnect_notify_t>(coordinator, peer_addr);
    }
    parent_t::on_child_shutdown(actor, ec);
}

void peer_supervisor_t::on_start() noexcept {
    spdlog::trace("peer_supervisor_t::on_start (addr = {})", (void *)address.get());
    parent_t::on_start();
}

void peer_supervisor_t::on_connect_request(message::connect_request_t &msg) noexcept {
    auto &payload = msg.payload.request_payload->payload;
    auto timeout = r::pt::milliseconds{bep_config.connect_timeout};

    auto builder = create_actor<peer_actor_t>()
                       .ssl_pair(&ssl_pair)
                       .device_name(device_name)
                       .bep_config(bep_config)
                       .coordinator(coordinator)
                       .timeout(timeout);
    auto peer_addr = std::visit(
        [&, builder = std::move(builder)](auto &&arg) mutable -> r::address_ptr_t {
            using P = payload::connect_request_t;
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, P::connect_info_t>) {
                auto &peer_id = arg.device_id;
                auto &uris = arg.uris;
                timeout *= uris.size();
                spdlog::trace("peer_supervisor_t::on_connect, initiating connection with {}", peer_id);
                return std::move(builder).peer_device_id(peer_id).uris(uris).finish()->get_address();
            } else if constexpr (std::is_same_v<T, P::connected_info_t>) {
                std::stringstream ss;
                ss << arg.remote;
                return std::move(builder)
                    .sock(std::optional<tcp_socket_t>(std::move(arg.sock)))
                    .peer_identity(ss.str())
                    .finish()
                    ->get_address();
            } else {
                static_assert(always_false_v<T>, "non-exhaustive visitor!");
            }
        },
        payload);
    addr2req.emplace(peer_addr, &msg);
}

void peer_supervisor_t::on_connect_notify(message::connect_notify_t &msg) noexcept {
    auto &peer_addr = msg.payload.peer_addr;
    auto &peer_id = msg.payload.peer_device_id;
    addr2id.emplace(peer_addr, peer_id);
    id2addr.emplace(peer_id, peer_addr);
    auto it = addr2req.find(peer_addr);
    reply_to(*it->second, peer_addr, peer_id, std::move(msg.payload.cluster_config));
    addr2req.erase(it);
}
