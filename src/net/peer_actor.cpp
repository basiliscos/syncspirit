#include "peer_actor.h"
#include "names.h"
#include "announce.pb.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t resolving = 0;
r::plugin::resource_id_t uris = 1;
r::plugin::resource_id_t io = 2;
} // namespace resource
} // namespace

peer_actor_t::peer_actor_t(config_t &config)
    : r::actor_base_t{config}, device_id{config.peer_device_id}, contact{config.contact}, ssl_pair{*config.ssl_pair} {}

void peer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&peer_actor_t::on_resolve);
        resources->acquire(resource::uris);
        try_next_uri();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::resolver, resolver).link(false); });
}

void peer_actor_t::try_next_uri() noexcept {
    transport::ssl_junction_t ssl{device_id, &ssl_pair, false};
    while (uri_idx < (std::int32_t)contact.uris.size()) {
        auto &uri = contact.uris[++uri_idx];
        auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
        transport::transport_config_t cfg{transport::ssl_option_t(ssl), uri, *sup};
        auto result = transport::initiate(cfg);
        if (result) {
            return initiate(std::move(result), uri);
        }
    }

    resources->release(resource::uris);
    spdlog::trace("peer_actor_t::try_next_uri, no way to conenct with {} found, shut down", device_id);
    do_shutdown();
}

void peer_actor_t::initiate(transport::transport_sp_t tran, const utils::URI &url) noexcept {
    transport = std::move(tran);

    spdlog::trace("peer_actor_t::try_next_uri(), will initate connection with {} via {}", device_id, url.full);
    pt::time_duration resolve_timeout = init_timeout / 2;
    auto port = std::to_string(url.port);
    request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
    resources->acquire(resource::resolving);
    return;
}

void peer_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    resources->release(resource::resolving);

    auto &ec = res.payload.ec;
    if (ec) {
        spdlog::warn("peer_actor_t::on_resolve error: {} ({})", ec.message(), ec.category().name());
        return try_next_uri();
    }

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_connect(addresses, on_connect, on_error);
    pt::time_duration timeout = init_timeout * 8 / 10;
    timer_request = start_timer(timeout, *this, &peer_actor_t::on_timer);
    resources->acquire(resource::io);
}

void peer_actor_t::on_connect(resolve_it_t) noexcept {
    spdlog::trace("peer_actor_t::on_connect(), device_id = {}", device_id);

    transport::handshake_fn_t handshake_fn([&](auto arg) { on_handshake(arg); });
    transport::error_fn_t error_fn([&](auto arg) { on_handshake_error(arg); });
    transport->async_handshake(handshake_fn, error_fn);
}

void peer_actor_t::on_io_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_io_error :: {}", ec.message());
    }
    if (timer_request)
        cancel_timer(*timer_request);
    do_shutdown();
}

void peer_actor_t::on_handshake(bool valid_peer) noexcept {
    spdlog::trace("peer_actor_t::on_handshake, device_id = {}, valid = {} ", device_id, valid_peer);
    do_shutdown();
}
void peer_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_handshake_error :: {}", ec.message());
    }
    do_shutdown();
}

void peer_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    spdlog::trace("peer_actor_t::on_timer_trigger, device_id = {}", device_id);
    do_shutdown();
}
