#include "global_discovery_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "../utils/beast_support.h"

// for convenience
using json = nlohmann::json;

using namespace syncspirit::net;

global_discovery_actor_t::global_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, endpoint{cfg.endpoint}, announce_url{cfg.announce_url}, rx_buff_size{cfg.rx_buff_size} {

    rx_buff = std::make_shared<rx_buff_t::element_type>(rx_buff_size);

    ssl_context = std::make_shared<ssl::context>(ssl::context::tls);
    ssl_context->set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);
    ssl_context->use_certificate_chain_file(cfg.cert_file);
    ssl_context->use_private_key_file(cfg.key_file, ssl::context::pem);
}

void global_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::http10, http_client, true).link(true); });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&global_discovery_actor_t::on_announce); });
}

void global_discovery_actor_t::on_start() noexcept {
    spdlog::trace("global_discovery_actor_t::on_start");
    json payload = json::object();
    payload["addresses"] = {fmt::format("tcp://{0}:{1}", endpoint.address().to_string(), endpoint.port())};

    http::request<http::string_body> req;
    req.method(http::verb::post);
    req.version(11);
    req.target(announce_url.path);
    req.set(http::field::host, announce_url.host);
    req.set(http::field::content_type, "application/json");

    req.body() = payload.dump();
    req.prepare_payload();

    fmt::memory_buffer tx_buff;
    auto res = utils::serialize(req, tx_buff);
    assert(res);
    spdlog::debug("data = {}", std::string(tx_buff.begin(), tx_buff.end()));
    auto timeout = shutdown_timeout / 2;
    request<payload::http_request_t>(http_client, announce_url, std::move(tx_buff), rx_buff, rx_buff_size, ssl_context)
        .send(timeout);
}

void global_discovery_actor_t::on_announce(message::http_response_t &message) noexcept {
    spdlog::trace("global_discovery_actor_t::on_announce");
    auto &ec = message.payload.ec;
    if (ec) {
        spdlog::error("global_discovery_actor_t, announcing error = {}", ec.message());
        return do_shutdown();
    }
    auto &res = message.payload.res->response;
    auto code = res.result_int();
    spdlog::debug("global_discovery_actor_t::on_announce code = {} ", code);
    if (code != 204) {
        spdlog::warn("global_discovery_actor_t, unexpected resonse code = {}", code);
        do_shutdown();
    }
}

#if 0
global_discovery_actor_t::global_discovery_actor_t(ra::supervisor_asio_t &sup,
                                                   const config::global_announce_config_t &cfg_)
    : r::actor_base_t{sup}, cfg{cfg_}, strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, ssl_context{ssl::context::tls}, resolver{io_context},
      stream{io_context, ssl_context}, timer{io_context}, activities_flag{0} {}

void global_discovery_actor_t::on_initialize(r::message::init_request_t &msg) noexcept {
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

void global_discovery_actor_t::on_shutdown(r::message::shutdown_request_t &msg) noexcept {
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
#endif
