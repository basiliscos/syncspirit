#include "peer_supervisor.h"
#include "peer_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "model/diff/peer/peer_state.h"

using namespace syncspirit::net;

template <class> inline constexpr bool always_false_v = false;

peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg)
    : parent_t{cfg}, device_name{cfg.device_name}, ssl_pair{*cfg.ssl_pair}, bep_config(cfg.bep_config) {
    log = utils::get_logger("net.peer_supervisor");
}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("peer_supervisor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::peers, get_address());
        p.discover_name(names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&peer_supervisor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&peer_supervisor_t::on_connect_request);
    });
}

void peer_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    using namespace model::diff;
    auto &peer_addr = actor->get_address();
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "{}, on_child_shutdown, {} due to {} ", identity, actor->get_identity(), reason->message());
    auto it_req = addr2req.find(peer_addr);
    if (it_req != addr2req.end()) {
        auto inner = utils::make_error_code(utils::error_code_t::cannot_connect_to_peer);
        reply_with_error(*it_req->second, make_error(inner, reason));
        addr2req.erase(it_req);
    } else {
        auto it = addr2id.find(peer_addr);
        assert(it != addr2id.end());
        auto &device_id = it->second;
        auto diff = cluster_diff_ptr_t();
        diff = new peer::peer_state_t(device_id, peer_addr, false);
        send<payload::model_update_t>(coordinator, std::move(diff));
        auto it_id = id2addr.find(device_id);
        id2addr.erase(it_id);
        addr2id.erase(it);
    }
    parent_t::on_child_shutdown(actor);
}

void peer_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
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
                LOG_TRACE(log, "{}, on_connect, initiating connection with {}", identity, peer_id);
                return std::move(builder).peer_device_id(peer_id).uris(uris).finish()->get_address();
            } else if constexpr (std::is_same_v<T, P::connected_info_t>) {
                return std::move(builder)
                    .sock(std::optional<tcp_socket_t>(std::move(arg.sock)))
                    .finish()
                    ->get_address();
            } else {
                static_assert(always_false_v<T>, "non-exhaustive visitor!");
            }
        },
        payload);
    addr2req.emplace(peer_addr, &msg);
}

void peer_supervisor_t::on_model_update(net::message::model_update_t& msg) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *msg.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto peer_supervisor_t::operator()(const model::diff::peer::peer_state_t &state) noexcept -> outcome::result<void>{
    if(state.online) {
        auto &peer_addr = state.peer_addr;
        auto &peer_id = state.peer_id;
        addr2id.emplace(peer_addr, peer_id);
        id2addr.emplace(peer_id, peer_addr);
        auto it = addr2req.find(peer_addr);
        reply_to(*it->second, peer_id);
        addr2req.erase(it);
    }
    return outcome::success();
}

