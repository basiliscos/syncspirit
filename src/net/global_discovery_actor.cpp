#include "global_discovery_actor.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;
namespace ssl = boost::asio::ssl;
namespace pt = boost::posix_time;

global_discovery_actor_t::global_discovery_actor_t(ra::supervisor_asio_t &sup,
                                                   const config::global_announce_config_t &cfg_)
    : r::actor_base_t{sup}, cfg{cfg_}, strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, ssl_context{ssl::context::tls}, resolver{io_context},
      stream{io_context, ssl_context}, timer{io_context}, activities_flag{0} {}

void global_discovery_actor_t::on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept {
    spdlog::trace("global_discovery_actor_t::on_initialize");

    ssl_context.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);
    ssl_context.use_certificate_chain_file(cfg.cert_file);
    ssl_context.use_private_key_file(cfg.key_file, ssl::context::pem);
    // Set SNI Hostname (many hosts need this to handshake successfully)
    if (!SSL_set_tlsext_host_name(stream.native_handle(), cfg.server_url.host.c_str())) {
        sys::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
        spdlog::error("global_discovery_actor:: Set SNI Hostname : {}", ec.message());
        trigger_shutdown();
        return;
    }
    spdlog::trace("ssl socket has been initialized");

    r::actor_base_t::on_initialize(msg);
}

void global_discovery_actor_t::trigger_shutdown() noexcept {
    if (!(activities_flag & SHUTDOWN_ACTIVE)) {
        activities_flag |= SHUTDOWN_ACTIVE;
        do_shutdown();
    }
}

void global_discovery_actor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    spdlog::trace("global_discovery_actor::on_shutdown");

    if (activities_flag & TIMER_ACTIVE) {
        sys::error_code ec;
        timer.cancel(ec);
        if (ec) {
            spdlog::error("global_discovery_actor:: timer cancellaction : {}", ec.message());
        }
        activities_flag &= ~TIMER_ACTIVE;
    }

    if (activities_flag & RESOLVER_ACTIVE) {
        resolver.cancel();
        activities_flag &= ~RESOLVER_ACTIVE;
    }

    if (activities_flag & SOCKET_ACTIVE) {
        sys::error_code ec;
        stream.next_layer().cancel(ec);
        if (ec) {
            spdlog::error("global_discovery_actor:: socket cancellaction : {}", ec.message());
        }
        activities_flag &= ~SOCKET_ACTIVE;
    }

    r::actor_base_t::on_shutdown(msg);
}

void global_discovery_actor_t::on_timeout_trigger() noexcept {
    spdlog::error("global_discovery_actor:: timeout");
    activities_flag &= ~TIMER_ACTIVE;
    trigger_shutdown();
}

void global_discovery_actor_t::on_timeout_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~TIMER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("global_discovery_actor::on_timer_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void global_discovery_actor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("global_discovery_actor_t::on_start");

    /* start timer */
    timer.expires_from_now(pt::seconds{cfg.timeout});
    auto fwd_timeout = ra::forwarder_t(*this, &global_discovery_actor_t::on_timeout_trigger,
                                       &global_discovery_actor_t::on_timeout_error);
    timer.async_wait(std::move(fwd_timeout));
    activities_flag |= TIMER_ACTIVE;

    /* start resolver */
    auto &url = cfg.server_url;
    spdlog::trace("global_discovery_actor resolving {}:{}", url.host, url.port);

    auto fwd_resolver =
        ra::forwarder_t(*this, &global_discovery_actor_t::on_resolve, &global_discovery_actor_t::on_resolve_error);
    resolver.async_resolve(cfg.server_url.host, url.proto, std::move(fwd_resolver));
    activities_flag |= RESOLVER_ACTIVE;

    r::actor_base_t::on_start(msg);
}

void global_discovery_actor_t::on_resolve_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~RESOLVER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("global_discovery_actor_t::on_resolve_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void global_discovery_actor_t::on_resolve(resolve_results_t results) noexcept {
    spdlog::trace("global_discovery_actor_t::on_resolve");
    activities_flag &= ~RESOLVER_ACTIVE;
    activities_flag |= SOCKET_ACTIVE;
    auto fwd =
        ra::forwarder_t(*this, &global_discovery_actor_t::on_connect, &global_discovery_actor_t::on_connect_error);
    asio::async_connect(stream.next_layer(), results.begin(), results.end(), std::move(fwd));
}

void global_discovery_actor_t::on_connect_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~SOCKET_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("gvcp_actor::on_connect_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void global_discovery_actor_t::on_connect(resolve_it_t it) noexcept {
    std::stringstream buff;
    buff << it->endpoint();
    spdlog::trace("global_discovery_actor::on_connect success ({})", buff.str());
    auto fwd =
        ra::forwarder_t(*this, &global_discovery_actor_t::on_handshake, &global_discovery_actor_t::on_handshake_error);
    stream.async_handshake(ssl::stream_base::client, std::move(fwd));
}

void global_discovery_actor_t::on_handshake_error(const sys::error_code &ec) noexcept {
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("gvcp_actor::on_handshake_error :: {}", ec.message());
        trigger_shutdown();
    }
}

void global_discovery_actor_t::on_handshake() noexcept {
    spdlog::trace("global_discovery_actor::on_handshake success");
    trigger_shutdown();
}
