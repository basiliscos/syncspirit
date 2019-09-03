#include "ssdp_actor.h"
#include "../utils/upnp_support.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;

static const constexpr std::size_t RX_BUFF_SIZE = 1500;

ssdp_actor_t::ssdp_actor_t(ra::supervisor_asio_t &sup, std::uint32_t max_wait_)
    : r::actor_base_t::actor_base_t(sup), strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, max_wait{max_wait_}, activities_flag{0} {
    rx_buff.resize(RX_BUFF_SIZE);
}

void ssdp_actor_t::cancel_pending() noexcept {
    if (activities_flag & (UDP_SEND | UDP_RECV)) {
        sys::error_code ec;
        sock->cancel(ec);
        if (ec) {
            spdlog::error("ssdp_actor_t:: udp socket cancellation : {}", ec.message());
        }
    }
    request.reset();
}

void ssdp_actor_t::init_start() noexcept {
    subscribe(&ssdp_actor_t::on_request);
    r::actor_base_t::init_start();
}

void ssdp_actor_t::on_request(message::ssdp_request_t &msg) noexcept {
    if (request) {
        spdlog::error("request is already processing");
        return do_shutdown();
    }
    request.reset(&msg);
    initate_discovery();
}

void ssdp_actor_t::shutdown_start() noexcept {
    spdlog::trace("ssdp_actor_t::shutdown_start");
    cancel_pending();
    r::actor_base_t::shutdown_start();
}

void ssdp_actor_t::on_udp_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~(UDP_RECV | UDP_SEND);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("ssdp_actor_t::on_udp_error :: {}", ec.message());
        if (request) {
            reply_with_error(*request, ec);
        }
        sys::error_code ec;
        sock->cancel(ec);
    }
}

void ssdp_actor_t::reply_error(const sys::error_code &ec) noexcept {
    reply_with_error(*request, ec);
    cancel_pending();
}

void ssdp_actor_t::initate_discovery() noexcept {
    /* some operations might be pending (i.e. timer cancellation) */
    if (activities_flag) {
        auto fwd_postpone = ra::forwarder_t(*this, &ssdp_actor_t::initate_discovery);
        return asio::post(io_context, fwd_postpone);
    }

    sock = std::make_unique<udp_socket_t>(io_context, udp::endpoint(udp::v4(), 0));

    /* broadcast discorvery */
    auto destination = udp::endpoint(v4::from_string(upnp_addr), upnp_port);
    auto request_result = make_discovery_request(tx_buff, max_wait);
    if (!request_result) {
        spdlog::error("ssdp_actor_t:: cannot serialize discovery request: {}", request_result.error().message());
        return reply_error(request_result.error());
    }

    spdlog::trace("ssdp_actor_t:: sending multicast request to {}:{} ({} bytes)", upnp_addr, upnp_port, tx_buff.size());

    auto fwd_discovery = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_sent, &ssdp_actor_t::on_udp_error);
    auto buff_tx = asio::buffer(tx_buff.data(), tx_buff.size());
    sock->async_send_to(buff_tx, destination, std::move(fwd_discovery));
    activities_flag |= UDP_SEND;

    auto fwd_recieve = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_received, &ssdp_actor_t::on_udp_error);
    auto buff_rx = asio::buffer(rx_buff.data(), RX_BUFF_SIZE);
    sock->async_receive(buff_rx, std::move(fwd_recieve));
    activities_flag |= UDP_RECV;
}

void ssdp_actor_t::on_discovery_sent(std::size_t bytes) noexcept {
    spdlog::trace("ssdp_actor_t::on_discovery_sent ({} bytes)", bytes);
    activities_flag &= ~UDP_SEND;

    auto endpoint = sock->local_endpoint();
    spdlog::trace("ssdp_actor_t::will wait discovery reply via {0}:{1}", endpoint.address().to_string(),
                  endpoint.port());
}

void ssdp_actor_t::on_discovery_received(std::size_t bytes) noexcept {
    spdlog::trace("ssdp_actor_t::on_discovery_received ({} bytes)", bytes);
    activities_flag &= ~UDP_RECV;

    if (!bytes) {
        auto ec = sys::errc::make_error_code(sys::errc::bad_message);
        return reply_error(ec);
    }

    const char *buff = static_cast<const char *>(rx_buff.data());
    auto discovery_result = parse(buff, bytes);
    if (!discovery_result) {
        spdlog::warn("upnp_actor:: can't get discovery result: {}", discovery_result.error().message());
        return reply_error(discovery_result.error());
    }

    auto &&value = discovery_result.value();
    reply_to(*request, std::move(value));
    request.reset();
}
