#include "upnp_actor.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;
using namespace syncspirit::utils;

using udp = boost::asio::ip::udp;
using tcp = boost::asio::ip::tcp;
using v4 = asio::ip::address_v4;

upnp_actor_t::upnp_actor_t(ra::supervisor_asio_t &sup, const config::upnp_config_t &cfg_)
    : r::actor_base_t{sup}, cfg{cfg_}, strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, resolver{io_context},
      udp_socket{io_context, udp::endpoint(udp::v4(), 0)}, timer{io_context}, activities_flag{0} {}

void upnp_actor_t::on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept {
    r::actor_base_t::on_initialize(msg);
    subscribe(&upnp_actor_t::on_description);
    subscribe(&upnp_actor_t::on_external_ip);
}

void upnp_actor_t::trigger_shutdown() noexcept {
    if (!(activities_flag & SHUTDOWN_ACTIVE)) {
        activities_flag |= SHUTDOWN_ACTIVE;
        do_shutdown();
    }
}

void upnp_actor_t::on_timeout_trigger() noexcept {
    spdlog::error("upnp_actor_t:: timeout");
    activities_flag &= ~TIMER_ACTIVE;
    trigger_shutdown();
}

void upnp_actor_t::on_timeout_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~TIMER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("upnp_actor_t::on_timer_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void upnp_actor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    spdlog::trace("upnp_actor_t::on_shutdown");

    if (activities_flag & TIMER_ACTIVE) {
        sys::error_code ec;
        timer.cancel(ec);
        if (ec) {
            spdlog::error("upnp_actor_t:: timer cancellation : {}", ec.message());
        }
        activities_flag &= ~TIMER_ACTIVE;
    }

    if (activities_flag & UDP_ACTIVE) {
        sys::error_code ec;
        udp_socket.cancel(ec);
        if (ec) {
            spdlog::error("upnp_actor_t:: udp socket cancellation : {}", ec.message());
        }
        activities_flag &= ~UDP_ACTIVE;
    }

    if (activities_flag & RESOLVER_ACTIVE) {
        resolver.cancel();
        activities_flag &= ~RESOLVER_ACTIVE;
    }

    if (activities_flag & TCP_ACTIVE) {
        sys::error_code ec;
        tcp_socket->cancel(ec);
        if (ec) {
            spdlog::error("upnp_actor_t:: tcp socket cancellation : {}", ec.message());
        }
        activities_flag &= ~TCP_ACTIVE;
    }

    r::actor_base_t::on_shutdown(msg);
}

void upnp_actor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("upnp_actor::on_start");

    /* start timer */
    timer.expires_from_now(pt::seconds{cfg.timeout});
    auto fwd_timeout = ra::forwarder_t(*this, &upnp_actor_t::on_timeout_trigger, &upnp_actor_t::on_timeout_error);
    timer.async_wait(std::move(fwd_timeout));
    activities_flag |= TIMER_ACTIVE;

    trigger_discovery();
    r::actor_base_t::on_start(msg);
}

void upnp_actor_t::trigger_discovery() noexcept {
    auto destination = udp::endpoint(v4::from_string(upnp_addr), upnp_port);
    auto request_result = make_discovery_request(tx_buff, cfg.max_wait);
    if (!request_result) {
        spdlog::error("upnp_actor:: can't serialize upnp discovery request: {}", request_result.error().message());
        return trigger_shutdown();
    }

    spdlog::trace("upnp_actor:: sending multicast request to {}:{} ({} bytes)", upnp_addr, upnp_port, tx_buff.size());

    auto fwd_discovery = ra::forwarder_t(*this, &upnp_actor_t::on_discovery_sent, &upnp_actor_t::on_udp_error);
    auto buff = asio::buffer(tx_buff.data(), tx_buff.size());
    udp_socket.async_send_to(buff, destination, std::move(fwd_discovery));
    activities_flag |= UDP_ACTIVE;
}

void upnp_actor_t::on_udp_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~UDP_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("upnp_actor_t::on_udp_error :: {}", ec.message());
        return trigger_shutdown();
    }
}

void upnp_actor_t::on_discovery_sent(std::size_t bytes) noexcept {
    spdlog::trace("upnp_actor_t::on_discovery_sent");
    activities_flag &= ~UDP_ACTIVE;
    if (bytes != tx_buff.size()) {
        spdlog::warn("upnp_actor_t::on_discovery_sent :: tx buff size mismatch {} vs {}", bytes, tx_buff.size());
        return trigger_shutdown();
    }

    spdlog::trace("upnp_actor::will wait discovery reply via {}");
    auto fwd = ra::forwarder_t(*this, &upnp_actor_t::on_discovery_received, &upnp_actor_t::on_udp_error);
    auto buff = rx_buff.prepare(cfg.rx_buff_size);
    udp_socket.async_receive(buff, std::move(fwd));
    activities_flag |= UDP_ACTIVE;
}

void upnp_actor_t::on_discovery_received(std::size_t bytes) noexcept {
    spdlog::trace("upnp_actor_t::on_discovery_received ({} bytes)", bytes);
    activities_flag &= ~UDP_ACTIVE;
    if (!bytes) {
        return trigger_discovery();
    }

    const char *buff = static_cast<const char *>(rx_buff.data().data());
    auto discovery_result = parse(buff, bytes);
    if (!discovery_result) {
        spdlog::warn("upnp_actor:: can't get discovery result: {}", discovery_result.error().message());
        return trigger_shutdown();
    }
    rx_buff.consume(bytes);
    discovery_option = discovery_result.value();

    trigger_request(
        [&]() {
            return std::make_tuple(make_description_request(tx_buff, *discovery_option), discovery_option->location);
        },
        [this](std::size_t bytes) { return send<resp_description_t>(address, bytes); });
}

void upnp_actor_t::on_resolve_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~RESOLVER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("upnp_actor_t::on_resolve_error :: {}", ec.message());
        return trigger_shutdown();
    }
}

void upnp_actor_t::on_resolve(resolve_results_t results) noexcept {
    spdlog::trace("upnp_actor_t::on_resolve");
    activities_flag &= ~RESOLVER_ACTIVE;

    tcp_socket = std::make_unique<tcp_socket_t>(io_context);
    activities_flag |= TCP_ACTIVE;

    auto fwd = ra::forwarder_t(*this, &upnp_actor_t::on_connect, &upnp_actor_t::on_tcp_error);
    asio::async_connect(*tcp_socket, results.begin(), results.end(), std::move(fwd));
}

void upnp_actor_t::on_tcp_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~TCP_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("upnp_actor_t::on_tcp_error :: {}", ec.message());
        return trigger_shutdown();
    }
}

void upnp_actor_t::on_connect(resolve_it_t it) noexcept {
    sys::error_code ec;
    auto local_endpoint = tcp_socket->local_endpoint(ec);
    if (ec) {
        spdlog::warn("upnp_actor_t::on_discovery_sent :: cannot get local endpoint: {}", ec.message());
        return trigger_shutdown();
    }
    auto remote_endpoint = it->endpoint();
    spdlog::trace("upnp_actor_t::on_connect {0}:{1} => {2}:{3}", local_endpoint.address().to_string(),
                  local_endpoint.port(), remote_endpoint.address().to_string(), remote_endpoint.port());

    spdlog::trace("upnp_actor:: making request ({} bytes) to {} ", tx_buff.size(), request_url.full);
    auto fwd = ra::forwarder_t(*this, &upnp_actor_t::on_request_sent, &upnp_actor_t::on_tcp_error);
    auto buff = asio::buffer(tx_buff.data(), tx_buff.size());
    asio::async_write(*tcp_socket, buff, std::move(fwd));
}

void upnp_actor_t::on_request_sent(std::size_t) noexcept {
    spdlog::trace("upnp_actor_t::on_on_request_sent");
    auto fwd = ra::forwarder_t(*this, &upnp_actor_t::on_response_received, &upnp_actor_t::on_tcp_error);
    rx_buff.prepare(cfg.rx_buff_size);
    http::async_read(*tcp_socket, rx_buff, *response_option, std::move(fwd));
}

void upnp_actor_t::on_response_received(std::size_t bytes) noexcept {
    spdlog::trace("upnp_actor_t::on_response_received ({}  bytes)", bytes);
    activities_flag &= ~TCP_ACTIVE;
    (*callback_option)(bytes);
    tcp_socket.reset();
    callback_option.reset();
}

void upnp_actor_t::on_description(r::message_t<resp_description_t> &msg) noexcept {
    auto &body = response_option->body();
    auto igd_result = parse_igd(body.data(), body.size());
    if (!igd_result) {
        spdlog::warn("upnp_actor:: can't get IGD result: {}", igd_result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return trigger_shutdown();
    }

    rx_buff.consume(msg.payload.bytes);
    auto &igd = igd_result.value();
    auto &location = discovery_option->location;
    std::string igd_control_url = fmt::format("http://{0}:{1}{2}", location.host, location.port, igd.control_path);
    std::string igd_descr_url = fmt::format("http://{0}:{1}{2}", location.host, location.port, igd.description_path);
    spdlog::debug("IGD control url: {0}, description url: {1}", igd_control_url, igd_descr_url);

    auto url_option = utils::parse(igd_control_url.c_str());
    if (!url_option) {
        spdlog::error("upnp_actor:: can't parse IGD url {}", igd_control_url);
        return trigger_shutdown();
    }
    igd_url = *url_option;

    trigger_request([&]() { return std::make_tuple(make_external_ip_request(tx_buff, igd_url), igd_url); },
                    [this](std::size_t bytes) { return send<resp_external_ip_t>(address, bytes); });
}

void upnp_actor_t::on_external_ip(r::message_t<resp_external_ip_t> &msg) noexcept {
    auto &body = response_option->body();
    auto ip_addr_result = parse_external_ip(body.data(), body.size());
    if (!ip_addr_result) {
        spdlog::warn("upnp_actor:: can't get external IP address: {}", ip_addr_result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return trigger_shutdown();
    }
    spdlog::debug("external IP addr: {}", ip_addr_result.value());
    rx_buff.consume(msg.payload.bytes);
    return trigger_shutdown();
}
