// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "services_supervisor.h"
#include "net/acceptor_actor.h"
#include "net/cluster_supervisor.h"
#include "net/dialer_actor.h"
#include "net/http_actor.h"
#include "net/local_discovery_actor.h"
#include "net/global_discovery_actor.h"
#include "net/peer_supervisor.h"
#include "net/relay_actor.h"
#include "net/resolver_actor.h"
#include "net/names.h"
#include "net/ssdp_actor.h"
#include "utils/format.hpp"

using namespace syncspirit::net;

services_supervisor_t::services_supervisor_t(config_t &cfg)
    : parent_t{cfg}, app_config{cfg.app_config}, cluster{cfg.cluster}, sequencer(cfg.sequencer),
      ssl_pair{*cfg.ssl_pair} {
    coordinator = address;
}

void services_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.services", false);
        log = utils::get_logger(identity);
        coordinator = get_supervisor().get_address();
    });
}

void services_supervisor_t::on_child_shutdown(actor_base_t *actor) noexcept {
    parent_t::on_child_shutdown(actor);
    auto &reason = actor->get_shutdown_reason();
    if (state == r::state_t::OPERATIONAL) {
        LOG_DEBUG(log, "on_child_shutdown, child {} termination: {}", actor->get_identity(), reason);
    }
}

void services_supervisor_t::on_start() noexcept {
    parent_t::on_start();
    LOG_TRACE(log, "on_start");
    launch_acceptor();
    launch_dialer();
    launch_http10();
    launch_local_discovery();
    launch_global_discovery();
    launch_peer_supervisor();
    launch_relay();
    launch_resolver();
    launch_upnp();
}

void services_supervisor_t::launch_acceptor() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    if (app_config.acceptor_config.enabled) {
        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 9 / 10;
            return create_actor<acceptor_actor_t>().timeout(timeout).cluster(cluster).spawner_address(spawner).finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }
}

void services_supervisor_t::launch_local_discovery() noexcept {
    if (app_config.local_announce_config.enabled) {
        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 9 / 10;
            auto &cfg = app_config.local_announce_config;
            return create_actor<local_discovery_actor_t>()
                .port(cfg.port)
                .frequency(cfg.frequency)
                .cluster(cluster)
                .timeout(timeout)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }
}

void services_supervisor_t::launch_relay() noexcept {
    if (app_config.relay_config.enabled) {
        auto timeout = shutdown_timeout * 9 / 10;
        auto io_timeout = shutdown_timeout * 8 / 10;
        create_actor<http_actor_t>()
            .timeout(timeout)
            .request_timeout(io_timeout)
            .resolve_timeout(io_timeout)
            .registry_name(names::http11_relay)
            .ssl_verify_store(app_config.ssl_verify_store)
            .keep_alive(true)
            .escalate_failure()
            .finish();

        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 9 / 10;
            return create_actor<relay_actor_t>()
                .timeout(timeout)
                .relay_config(app_config.relay_config)
                .cluster(cluster)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }
}

void services_supervisor_t::launch_upnp() noexcept {
    if (app_config.upnp_config.enabled) {
        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto timeout = shutdown_timeout * 8 / 10;
            return create_actor<ssdp_actor_t>()
                .timeout(timeout)
                .upnp_config(app_config.upnp_config)
                .cluster(cluster)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }
}

void services_supervisor_t::launch_resolver() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    auto io_timeout = shutdown_timeout * 8 / 10;
    create_actor<resolver_actor_t>().timeout(timeout).resolve_timeout(io_timeout).escalate_failure().finish();
}

void services_supervisor_t::launch_http10() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    auto io_timeout = shutdown_timeout * 8 / 10;
    create_actor<http_actor_t>()
        .timeout(timeout)
        .request_timeout(io_timeout)
        .resolve_timeout(io_timeout)
        .registry_name(names::http10)
        .keep_alive(false)
        .escalate_failure()
        .finish();
}

void services_supervisor_t::launch_global_discovery() noexcept {
    if (app_config.global_announce_config.enabled) {
        auto timeout = shutdown_timeout * 9 / 10;
        auto io_timeout = shutdown_timeout * 8 / 10;
        create_actor<http_actor_t>()
            .timeout(timeout)
            .request_timeout(io_timeout)
            .resolve_timeout(io_timeout)
            .registry_name(names::http11_gda)
            .ssl_verify_store(app_config.ssl_verify_store)
            .keep_alive(false)
            .escalate_failure()
            .finish();

        auto factory = [this](r::supervisor_t &, const r::address_ptr_t &spawner) -> r::actor_ptr_t {
            auto &gcfg = app_config.global_announce_config;
            auto timeout = shutdown_timeout * 9 / 10;
            return create_actor<global_discovery_actor_t>()
                .timeout(timeout)
                .cluster(cluster)
                .ssl_pair(&ssl_pair)
                .announce_url(gcfg.announce_url)
                .lookup_url(gcfg.lookup_url)
                .rx_buff_size(gcfg.rx_buff_size)
                .io_timeout(gcfg.timeout)
                .debug(gcfg.debug)
                .spawner_address(spawner)
                .finish();
        };
        spawn(factory).restart_period(pt::seconds{5}).restart_policy(r::restart_policy_t::fail_only).spawn();
    }
}

void services_supervisor_t::launch_dialer() noexcept {
    auto dcfg = app_config.dialer_config;
    if (dcfg.enabled) {
        auto timeout = shutdown_timeout * 9 / 10;
        create_actor<dialer_actor_t>()
            .timeout(timeout)
            .dialer_config(dcfg)
            .cluster(cluster)
            .escalate_failure()
            .finish();
    }
}

void services_supervisor_t::launch_peer_supervisor() noexcept {
    auto timeout = shutdown_timeout * 9 / 10;
    create_actor<peer_supervisor_t>()
        .cluster(cluster)
        .ssl_pair(&ssl_pair)
        .device_name(app_config.device_name)
        .strand(strand)
        .timeout(timeout)
        .bep_config(app_config.bep_config)
        .relay_config(app_config.relay_config)
        .escalate_failure()
        .finish();
}

void services_supervisor_t::launch_cluster_supervisor() noexcept {
    create_actor<cluster_supervisor_t>()
        .timeout(shutdown_timeout * 9 / 10)
        .strand(strand)
        .cluster(cluster)
        .sequencer(sequencer)
        .config(app_config)
        .escalate_failure()
        .finish();
}
