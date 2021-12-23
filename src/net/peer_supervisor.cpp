#include "peer_supervisor.h"
#include "peer_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "model/diff/peer/peer_state.h"
#include "model/diff/modify/connect_request.h"
#include "model/diff/modify/update_contact.h"

using namespace syncspirit::net;

template <class> inline constexpr bool always_false_v = false;

peer_supervisor_t::peer_supervisor_t(peer_supervisor_config_t &cfg)
    : parent_t{cfg}, cluster{cfg.cluster}, device_name{cfg.device_name}, ssl_pair{*cfg.ssl_pair}, bep_config(cfg.bep_config) {
    log = utils::get_logger("net.peer_supervisor");
}

void peer_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("peer_supervisor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&peer_supervisor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&peer_supervisor_t::on_contact_update, coordinator);
            }
        });
    });
}

void peer_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    using namespace model::diff;
    auto &peer_addr = actor->get_address();
    auto &reason = actor->get_shutdown_reason();
    LOG_TRACE(log, "{}, on_child_shutdown, {} due to {} ", identity, actor->get_identity(), reason->message());

    auto it = addr2id.find(peer_addr);
    if (it != addr2id.end()) {
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

void peer_supervisor_t::on_model_update(net::message::model_update_t& msg) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *msg.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void peer_supervisor_t::on_contact_update(net::message::contact_update_t& msg) noexcept {
    LOG_TRACE(log, "{}, on_contact_update", identity);
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
    }
    return outcome::success();
}

auto peer_supervisor_t::operator()(const model::diff::modify::connect_request_t &diff) noexcept -> outcome::result<void> {

    auto lock = std::unique_lock(diff.mutex);
    auto sock  = std::move(*diff.sock);
    diff.sock.reset();

    auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
    auto peer_addr =  create_actor<peer_actor_t>()
           .ssl_pair(&ssl_pair)
           .device_name(device_name)
           .bep_config(bep_config)
           .coordinator(coordinator)
           .timeout(timeout)
           .sock(std::optional(std::move(sock)))
           .finish()
           ->get_address();
    return outcome::success();
}

auto peer_supervisor_t::operator()(const model::diff::modify::update_contact_t &diff) noexcept -> outcome::result<void> {
    if (!diff.self && diff.known) {
        auto& devices = cluster->get_devices();
        auto peer = devices.by_sha256(diff.device.get_sha256());
        if (!peer->is_online()) {
            auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
            LOG_DEBUG(log, "{} initiating connection with {}", identity, peer->device_id());
            auto peer_addr =  create_actor<peer_actor_t>()
               .ssl_pair(&ssl_pair)
               .device_name(device_name)
               .bep_config(bep_config)
               .coordinator(coordinator)
               .timeout(timeout)
               .peer_device_id(diff.device)
               .uris(diff.uris)
               .finish()
               ->get_address();
        }
    }
    return outcome::success();
}
