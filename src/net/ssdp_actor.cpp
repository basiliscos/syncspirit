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
      max_wait{cfg.max_wait} {
    rx_buff.resize(RX_BUFF_SIZE);
}

void ssdp_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("ssdp", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator_addr, true).link(false); });
}

void ssdp_actor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    sock = std::make_unique<udp_socket_t>(strand.context(), udp::endpoint(udp::v4(), 0));

    /* broadcast discorvery */
    auto destination = udp::endpoint(v4::from_string(upnp_addr), upnp_port);
    auto request_result = make_discovery_request(tx_buff, max_wait);
    if (!request_result) {
        auto &ec = request_result.error();
        spdlog::error("{},  cannot serialize discovery request: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    spdlog::trace("{}, sending multicast request to {}:{} ({} bytes)", identity, upnp_addr, upnp_port, tx_buff.size());

    auto fwd_recieve = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_received, &ssdp_actor_t::on_udp_recv_error);
    auto buff_rx = asio::buffer(rx_buff.data(), RX_BUFF_SIZE);
    sock->async_receive(buff_rx, std::move(fwd_recieve));
    resources->acquire(resource::recv);

    auto fwd_discovery = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_sent, &ssdp_actor_t::on_udp_send_error);
    auto buff_tx = asio::buffer(tx_buff.data(), tx_buff.size());
    sock->async_send_to(buff_tx, destination, std::move(fwd_discovery));
    resources->acquire(resource::send);

    auto timeout = pt::seconds(max_wait);
    timer_request = start_timer(timeout, *this, &ssdp_actor_t::on_timer);
    resources->acquire(resource::timer);
}

void ssdp_actor_t::shutdown_start() noexcept {
    spdlog::trace("{}, shutdown_start", identity);
    sys::error_code ec;
    if (resources->has(resource::send) || resources->has(resource::recv)) {
        sock->cancel(ec);
        if (ec) {
            spdlog::error("{},  udp socket cancellation : {}", identity, ec.message());
        }
    }
    timer_cancel();
    r::actor_base_t::shutdown_start();
}

void ssdp_actor_t::on_discovery_sent(std::size_t bytes) noexcept {
    spdlog::trace("{}, on_discovery_sent ({} bytes)", identity, bytes);
    resources->release(resource::send);

    auto endpoint = sock->local_endpoint();
    spdlog::trace("{}, will wait discovery reply via {}:{}", identity, endpoint.address().to_string(), endpoint.port());
}

void ssdp_actor_t::on_discovery_received(std::size_t bytes) noexcept {
    spdlog::trace("{}, on_discovery_received ({} bytes)", identity, bytes);
    resources->release(resource::recv);

    if (!bytes || !resources->has(resource::timer)) {
        auto ec = r::make_error_code(r::shutdown_code_t::normal);
        return do_shutdown(make_error(ec));
    }

    const char *buff = static_cast<const char *>(rx_buff.data());
    auto discovery_result = parse(buff, bytes);
    if (!discovery_result) {
        auto &ec = discovery_result.error();
        spdlog::warn("{},  can't get discovery result: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    auto &&value = discovery_result.value();
    auto my_ip = sock->local_endpoint().address();

    send<payload::ssdp_notification_t>(coordinator_addr, std::move(value), my_ip);
    timer_cancel();
}

void ssdp_actor_t::on_udp_send_error(const sys::error_code &ec) noexcept {
    resources->release(resource::send);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("{}, on_udp_send_error :: {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
    timer_cancel();
}

void ssdp_actor_t::on_udp_recv_error(const sys::error_code &ec) noexcept {
    resources->release(resource::recv);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("{}, on_udp_recv_error :: {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
    timer_cancel();
}

void ssdp_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    if (!cancelled) {
        spdlog::debug("{}, on_timer_trigger", identity);
        auto ec = r::make_error_code(r::shutdown_code_t::normal);
        return do_shutdown(make_error(ec));
    }
}

void ssdp_actor_t::timer_cancel() noexcept {
    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }
}
