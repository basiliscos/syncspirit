#include "ssdp_actor.h"
#include "../utils/upnp_support.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;

using v4 = asio::ip::address_v4;

static const constexpr std::size_t RX_BUFF_SIZE = 1500;

ssdp_actor_t::ssdp_actor_t(ra::supervisor_asio_t &sup, std::uint32_t max_wait_)
    : r::actor_base_t::actor_base_t(sup), strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, timer{io_context}, sock{io_context, udp::endpoint(udp::v4(), 0)},
      max_wait{max_wait_}, activities_flag{0} {
    rx_buff.resize(RX_BUFF_SIZE);
}

void ssdp_actor_t::cancel_pending() noexcept {
    if (activities_flag & TIMER_ACTIVE) {
        sys::error_code ec;
        timer.cancel(ec);
        if (ec) {
            spdlog::error("ssdp_actor_t:: timer cancellation : {}", ec.message());
        }
        activities_flag &= ~TIMER_ACTIVE;
    }

    if (activities_flag & UDP_ACTIVE) {
        sys::error_code ec;
        sock.cancel(ec);
        if (ec) {
            spdlog::error("ssdp_actor_t:: udp socket cancellation : {}", ec.message());
        }
        activities_flag &= ~UDP_ACTIVE;
    }
}

void ssdp_actor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    spdlog::trace("ssdp_actor_t::on_shutdown");
    cancel_pending();
    r::actor_base_t::on_shutdown(msg);
}

void ssdp_actor_t::on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept {
    subscribe(&ssdp_actor_t::on_try_again);
    r::actor_base_t::on_initialize(msg);
}

void ssdp_actor_t::trigger_shutdown() noexcept {
    if (!(activities_flag & SHUTDOWN_ACTIVE)) {
        activities_flag |= SHUTDOWN_ACTIVE;
        do_shutdown();
    }
}

void ssdp_actor_t::on_timeout_trigger() noexcept {
    spdlog::warn("ssdp_actor_t:: timeout");
    activities_flag &= ~TIMER_ACTIVE;
    auto ec = sys::errc::make_error_code(sys::errc::timed_out);
    reply_error(ec);
}

void ssdp_actor_t::on_timeout_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~TIMER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("ssdp_actor_t::on_timer_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void ssdp_actor_t::on_udp_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~UDP_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("ssdp_actor_t::on_udp_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void ssdp_actor_t::reply_error(const sys::error_code &ec) noexcept {
    send<ssdp_failure_t>(supervisor.get_address(), ec);
    cancel_pending();
}

void ssdp_actor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("ssdp_actor_t::on_start");
    initate_discovery();
    r::actor_base_t::on_start(msg);
}

void ssdp_actor_t::on_try_again(r::message_t<try_again_request_t> &) noexcept {
    spdlog::trace("ssdp_actor_t::on_start");
    initate_discovery();
}

void ssdp_actor_t::initate_discovery() noexcept {
    /* broadcast discorvery */
    auto destination = udp::endpoint(v4::from_string(upnp_addr), upnp_port);
    auto request_result = make_discovery_request(tx_buff, max_wait);
    if (!request_result) {
        spdlog::error("ssdp_actor_t:: cannot serialize discovery request: {}", request_result.error().message());
        reply_error(request_result.error());
        return trigger_shutdown();
    }

    spdlog::trace("ssdp_actor_t:: sending multicast request to {}:{} ({} bytes)", upnp_addr, upnp_port, tx_buff.size());

    auto fwd_discovery = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_sent, &ssdp_actor_t::on_udp_error);
    auto buff = asio::buffer(tx_buff.data(), tx_buff.size());
    sock.async_send_to(buff, destination, std::move(fwd_discovery));
    activities_flag |= UDP_ACTIVE;

    /* start timer */
    timer.expires_from_now(pt::seconds{max_wait});
    auto fwd_timeout = ra::forwarder_t(*this, &ssdp_actor_t::on_timeout_trigger, &ssdp_actor_t::on_timeout_error);
    timer.async_wait(std::move(fwd_timeout));
    activities_flag |= TIMER_ACTIVE;
}

void ssdp_actor_t::on_discovery_sent(std::size_t bytes) noexcept {
    spdlog::trace("ssdp_actor_t::on_discovery_sent ({} bytes)", bytes);
    activities_flag &= ~UDP_ACTIVE;
    if (bytes != tx_buff.size()) {
        spdlog::warn("ssdp_actor_t::on_discovery_sent :: tx buff size mismatch {} vs {}", bytes, tx_buff.size());
        return trigger_shutdown();
    }

    auto endpoint = sock.local_endpoint();
    spdlog::trace("ssdp_actor_t::will wait discovery reply via {0}:{1}", endpoint.address().to_string(),
                  endpoint.port());
    auto fwd = ra::forwarder_t(*this, &ssdp_actor_t::on_discovery_received, &ssdp_actor_t::on_udp_error);
    auto buff = asio::buffer(rx_buff.data(), RX_BUFF_SIZE);
    sock.async_receive(buff, std::move(fwd));
    activities_flag |= UDP_ACTIVE;
}

void ssdp_actor_t::on_discovery_received(std::size_t bytes) noexcept {
    spdlog::trace("ssdp_actor_t::on_discovery_received ({} bytes)", bytes);
    activities_flag &= ~UDP_ACTIVE;

    sys::error_code ec;
    timer.cancel(ec);
    if (ec) {
        spdlog::error("ssdp_actor_t:: timer cancellation : {}", ec.message());
        return;
    }
    activities_flag &= ~TIMER_ACTIVE;

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

    send<ssdp_result_t>(supervisor.get_address(), discovery_result.value());
}
