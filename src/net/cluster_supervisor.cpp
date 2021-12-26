#include "cluster_supervisor.h"
#include "controller_actor.h"
#include "names.h"
#include "utils/error_code.h"
#include "hasher/hasher_proxy_actor.h"
#include "model/diff/peer/peer_state.h"

using namespace syncspirit::net;

cluster_supervisor_t::cluster_supervisor_t(cluster_supervisor_config_t &config)
    : ra::supervisor_asio_t{config}, bep_config{config.bep_config}, hasher_threads{config.hasher_threads},
      cluster{config.cluster} {
    log = utils::get_logger("net.cluster");
}

void cluster_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    ra::supervisor_asio_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net::cluster", false);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&cluster_supervisor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        auto& sup = get_supervisor();
        create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(init_timeout)
            .hasher_threads(hasher_threads)
            .name(net::names::hasher_proxy)
            .finish();
    });
}

void cluster_supervisor_t::on_start() noexcept {
    log->trace("{}, on_start", identity);
    ra::supervisor_asio_t::on_start();
}

void cluster_supervisor_t::shutdown_start() noexcept {
    log->trace("{}, shutdown_start", identity);
    ra::supervisor_asio_t::shutdown_start();
}

void cluster_supervisor_t::on_model_update(message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto cluster_supervisor_t::operator()(const model::diff::peer::peer_state_t &diff) noexcept -> outcome::result<void> {
    if (!cluster->is_tainted()) {
        auto peer = cluster->get_devices().by_sha256(diff.peer_id);
        LOG_TRACE(log, "{}, visiting peer_state_t, {} is online: {}", identity, peer->device_id(), (diff.online? "yes": "no"));
        if (diff.online) {
            /* auto addr = */
            create_actor<controller_actor_t>()
                            .bep_config(bep_config)
                            .timeout(init_timeout * 7 / 9)
                            .peer(peer)
                            .peer_addr(diff.peer_addr)
                            .request_timeout(pt::milliseconds(bep_config.request_timeout))
                            .cluster(cluster)
                            .finish();
        }
    }
    return outcome::success();
}


void cluster_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    LOG_TRACE(log, "{}, on_child_shutdown: {}({})", identity, actor->get_identity(), actor->use_count());
    ra::supervisor_asio_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        log->debug("{}, on_child_shutdown, child {} termination: {}", identity, actor->get_identity(),
                   reason->message());
    }
}
