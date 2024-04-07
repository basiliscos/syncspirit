// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "ssdp_actor.h"
#include "upnp_actor.h"
#include "proto/upnp_support.h"
#include "names.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;
using namespace syncspirit::proto;

static const constexpr std::size_t RX_BUFF_SIZE = 1500;

namespace {
namespace resource {
r::plugin::resource_id_t send = 0;
r::plugin::resource_id_t recv = 1;
r::plugin::resource_id_t timer = 2;
} // namespace resource
} // namespace

ssdp_actor_t::ssdp_actor_t(ssdp_actor_config_t &cfg)
    : r::actor_base_t::actor_base_t(cfg), cluster{cfg.cluster},
      strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()}, upnp_config{cfg.upnp_config},
      upnp_endpoint(v4::from_string(upnp_addr), upnp_port) {
    log = utils::get_logger("net.ssdp");
    rx_buff.resize(RX_BUFF_SIZE);
}

void ssdp_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("ssdp", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator, true).link(false); });
}

void ssdp_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    sock = std::make_unique<udp_socket_t>(strand.context(), udp::endpoint(udp::v4(), 0));

    /* broadcast discovery */
    auto max_wait = upnp_config.max_wait;
    auto request_result = make_discovery_request(tx_buff, max_wait);
    if (!request_result) {
        auto &ec = request_result.error();
        LOG_ERROR(log, "{},  cannot serialize discovery request: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    LOG_TRACE(log, "{}, sending multicast request to {}:{} ({} bytes)", identity, upnp_addr, upnp_port, tx_buff.size());

    auto fwd_receive = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_received, &ssdp_actor_t::on_udp_recv_error);
    auto buff_rx = asio::buffer(rx_buff.data(), RX_BUFF_SIZE);
    sock->async_receive(buff_rx, std::move(fwd_receive));
    resources->acquire(resource::recv);

    auto fwd_discovery = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_sent, &ssdp_actor_t::on_udp_send_error);
    auto buff_tx = asio::buffer(tx_buff.data(), tx_buff.size());
    sock->async_send_to(buff_tx, upnp_endpoint, std::move(fwd_discovery));
    resources->acquire(resource::send);

    auto timeout = pt::seconds(max_wait);
    timer_request = start_timer(timeout, *this, &ssdp_actor_t::on_timer);
    resources->acquire(resource::timer);
}

void ssdp_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    sys::error_code ec;
    if (resources->has(resource::send) || resources->has(resource::recv)) {
        sock->cancel(ec);
        if (ec) {
            LOG_ERROR(log, "{},  udp socket cancellation : {}", identity, ec.message());
        }
    }
    timer_cancel();
    r::actor_base_t::shutdown_start();
}

void ssdp_actor_t::on_discovery_sent(std::size_t bytes) noexcept {
    LOG_TRACE(log, "{}, on_discovery_sent ({} bytes)", identity, bytes);
    resources->release(resource::send);

    auto endpoint = sock->local_endpoint();
    LOG_TRACE(log, "{}, will wait discovery reply via {}:{}", identity, endpoint.address().to_string(),
              endpoint.port());
}

void ssdp_actor_t::on_discovery_received(std::size_t bytes) noexcept {
    LOG_TRACE(log, "{}, on_discovery_received ({} bytes)", identity, bytes);
    resources->release(resource::recv);

    if (!bytes || !resources->has(resource::timer)) {
        auto ec = r::make_error_code(r::shutdown_code_t::normal);
        return do_shutdown(make_error(ec));
    }

    const char *buff = static_cast<const char *>(rx_buff.data());
    auto discovery_result = parse(buff, bytes);
    if (!discovery_result) {
        auto &ec = discovery_result.error();
        LOG_WARN(log, "{},  can't get discovery result: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    auto &&value = discovery_result.value();
    auto &igd_location = value.location;

    launch_upnp(igd_location);
    timer_cancel();
    do_shutdown();
}

void ssdp_actor_t::on_udp_send_error(const sys::error_code &ec) noexcept {
    resources->release(resource::send);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "{}, on_udp_send_error :: {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
    timer_cancel();
}

void ssdp_actor_t::on_udp_recv_error(const sys::error_code &ec) noexcept {
    resources->release(resource::recv);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "{}, on_udp_recv_error :: {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
    timer_cancel();
}

void ssdp_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    if (!cancelled) {
        LOG_DEBUG(log, "{}, on_timer_trigger", identity);
    }
    auto ec = r::make_error_code(r::shutdown_code_t::normal);
    return do_shutdown(make_error(ec));
}

void ssdp_actor_t::timer_cancel() noexcept {
    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }
}

void ssdp_actor_t::launch_upnp(const URI &igd_uri) noexcept {
    LOG_DEBUG(log, "{}, launching upnp", identity);
    auto timeout = shutdown_timeout * 9 / 10;

    auto &sup = get_supervisor();
    sup.create_actor<upnp_actor_t>()
        .cluster(cluster)
        .timeout(timeout)
        .descr_url(igd_uri)
        .rx_buff_size(upnp_config.rx_buff_size)
        .external_port(upnp_config.external_port)
        .debug(upnp_config.debug)
        .finish()
        ->get_address();
}
