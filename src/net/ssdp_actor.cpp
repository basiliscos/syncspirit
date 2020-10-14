#include "ssdp_actor.h"
#include "../proto/upnp_support.h"
#include "spdlog/spdlog.h"
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
    : r::actor_base_t::actor_base_t(cfg), strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()},
      timer{strand}, max_wait{cfg.max_wait} {
    rx_buff.resize(RX_BUFF_SIZE);
}

void ssdp_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator_addr, true).link(false); });
}

void ssdp_actor_t::on_start() noexcept {
    spdlog::trace("ssdp_actor_t::on_start ({})", (void *)address.get());
    sock = std::make_unique<udp_socket_t>(strand.context(), udp::endpoint(udp::v4(), 0));

    /* broadcast discorvery */
    auto destination = udp::endpoint(v4::from_string(upnp_addr), upnp_port);
    auto request_result = make_discovery_request(tx_buff, max_wait);
    if (!request_result) {
        spdlog::error("ssdp_actor_t:: cannot serialize discovery request: {}", request_result.error().message());
        return do_shutdown();
    }

    spdlog::trace("ssdp_actor_t:: sending multicast request to {}:{} ({} bytes)", upnp_addr, upnp_port, tx_buff.size());

    auto fwd_recieve = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_received, &ssdp_actor_t::on_udp_recv_error);
    auto buff_rx = asio::buffer(rx_buff.data(), RX_BUFF_SIZE);
    sock->async_receive(buff_rx, std::move(fwd_recieve));
    resources->acquire(resource::recv);

    auto fwd_discovery = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_sent, &ssdp_actor_t::on_udp_send_error);
    auto buff_tx = asio::buffer(tx_buff.data(), tx_buff.size());
    sock->async_send_to(buff_tx, destination, std::move(fwd_discovery));
    resources->acquire(resource::send);

    auto timeout = pt::seconds(max_wait);
    timer.expires_from_now(timeout);
    auto fwd_timer = ra::forwarder_t(*this, &ssdp_actor_t::on_timer_trigger, &ssdp_actor_t::on_timer_error);
    timer.async_wait(std::move(fwd_timer));
    resources->acquire(resource::timer);
}

void ssdp_actor_t::shutdown_start() noexcept {
    spdlog::trace("ssdp_actor_t::shutdown_start");
    sys::error_code ec;
    if (resources->has(resource::send) || resources->has(resource::recv)) {
        sock->cancel(ec);
        if (ec) {
            spdlog::error("ssdp_actor_t:: udp socket cancellation : {}", ec.message());
        }
    }
    timer_cancel();
    r::actor_base_t::shutdown_start();
}

void ssdp_actor_t::on_discovery_sent(std::size_t bytes) noexcept {
    spdlog::trace("ssdp_actor_t::on_discovery_sent ({} bytes)", bytes);
    resources->release(resource::send);

    auto endpoint = sock->local_endpoint();
    spdlog::trace("ssdp_actor_t::will wait discovery reply via {0}:{1}", endpoint.address().to_string(),
                  endpoint.port());
}

void ssdp_actor_t::on_discovery_received(std::size_t bytes) noexcept {
    spdlog::trace("ssdp_actor_t::on_discovery_received ({} bytes)", bytes);
    resources->release(resource::recv);

    if (!bytes || !resources->has(resource::timer)) {
        return do_shutdown();
    }

    const char *buff = static_cast<const char *>(rx_buff.data());
    auto discovery_result = parse(buff, bytes);
    if (!discovery_result) {
        spdlog::warn("upnp_actor:: can't get discovery result: {}", discovery_result.error().message());
        return do_shutdown();
    }

    auto &&value = discovery_result.value();
    auto my_ip = sock->local_endpoint().address();

    send<payload::ssdp_notification_t>(coordinator_addr, std::move(value), my_ip);
    timer_cancel();
}

void ssdp_actor_t::on_udp_send_error(const sys::error_code &ec) noexcept {
    resources->release(resource::send);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("ssdp_actor_t::on_udp_send_error :: {}", ec.message());
        do_shutdown();
    }
    timer_cancel();
}

void ssdp_actor_t::on_udp_recv_error(const sys::error_code &ec) noexcept {
    resources->release(resource::recv);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("ssdp_actor_t::on_udp_recv_error :: {}", ec.message());
        do_shutdown();
    }
    timer_cancel();
}

void ssdp_actor_t::on_timer_error(const sys::error_code &ec) noexcept {
    resources->release(resource::timer);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("ssdp_actor_t::on_timer_error :: {}", ec.message());
        do_shutdown();
    }
}

void ssdp_actor_t::on_timer_trigger() noexcept {
    resources->release(resource::timer);
    spdlog::warn("ssdp_actor_t::on_timer_trigger");
    do_shutdown();
}

void ssdp_actor_t::timer_cancel() noexcept {
    if (resources->has(resource::timer)) {
        sys::error_code ec;
        timer.cancel(ec);
        if (ec) {
            spdlog::error("ssdp_actor_t:: timer cancellation : {}", ec.message());
        }
    }
}
