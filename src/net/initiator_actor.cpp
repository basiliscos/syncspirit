#include "initiator_actor.h"
#include "constants.h"
#include "names.h"
#include "model/messages.h"
#include "utils/error_code.h"
#include "model/diff/peer/peer_state.h"
#include <sstream>
#include <fmt/fmt.h>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t initializing = 0;
r::plugin::resource_id_t resolving = 1;
r::plugin::resource_id_t connect = 2;
r::plugin::resource_id_t handshake = 3;
} // namespace resource
} // namespace

initiator_actor_t::initiator_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, peer_device_id{cfg.peer_device_id}, uris{cfg.uris}, ssl_pair{*cfg.ssl_pair},
      sock(std::move(cfg.sock)), cluster{std::move(cfg.cluster)}, sink(std::move(cfg.sink)),
      custom(std::move(cfg.custom)), router{*cfg.router} {
    log = utils::get_logger("net.initator");
    active = !sock.has_value();
}

void initiator_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);

    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        if (active) {
            auto value = fmt::format("init/active:{}", peer_device_id.get_short());
            p.set_identity(value, false);
        } else {
            auto ep = sock.value().remote_endpoint();
            auto value = fmt::format("init/passive:{}", ep);
            p.set_identity(value, false);
        }
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&initiator_actor_t::on_resolve);
        resources->acquire(resource::initializing);
        if (active) {
            if (cluster) {
                auto diff = model::diff::cluster_diff_ptr_t();
                auto state = model::device_state_t::dialing;
                auto sha256 = peer_device_id.get_sha256();
                diff = new model::diff::peer::peer_state_t(*cluster, sha256, nullptr, state);
                send<model::payload::model_update_t>(coordinator, std::move(diff));
            }
            initiate_active();
        } else {
            initiate_passive();
        }
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::resolver, resolver).link(false);
        p.discover_name(names::coordinator, coordinator).link(false);
    });
}

void initiator_actor_t::initiate_active() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    while (uri_idx < uris.size()) {
        auto &uri = uris[uri_idx++];
        if (uri.proto == "tcp") {
            auto sup = static_cast<ra::supervisor_asio_t *>(&router);
            auto trans = transport::initiate_tls_active(*sup, ssl_pair, peer_device_id, uri);
            initiate(std::move(trans), uri);
            return;
        } else {
            LOG_DEBUG(log, "{}, unsupported proto '{}' for the url '{}'", identity, uri.proto, uri.full);
        }
    }

    LOG_TRACE(log, "{}, try_next_uri, no way to connect found, shut down", identity);
    auto ec = utils::make_error_code(utils::error_code_t::connection_impossible);
    do_shutdown(make_error(ec));
}

void initiator_actor_t::initiate_passive() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto sup = static_cast<ra::supervisor_asio_t *>(&router);
    transport = transport::initiate_tls_passive(*sup, ssl_pair, std::move(sock.value()));
    initiate_handshake();
}

void initiator_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "{}, on_start", identity);
    send<payload::peer_connected_t>(sink, std::move(transport), peer_device_id, remote_endpoint, std::move(custom));
    success = true;
    do_shutdown();
}

void initiator_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    if (resources->has(resource::initializing)) {
        resources->release(resource::initializing);
    }
    if (resources->has(resource::connect)) {
        transport->cancel();
    }
    if (resources->has(resource::handshake)) {
        transport->cancel();
    }
    r::actor_base_t::shutdown_start();
}

void initiator_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    if (active && !success && cluster) {
        auto diff = model::diff::cluster_diff_ptr_t();
        auto state = model::device_state_t::offline;
        auto sha256 = peer_device_id.get_sha256();
        diff = new model::diff::peer::peer_state_t(*cluster, sha256, nullptr, state);
        send<model::payload::model_update_t>(coordinator, std::move(diff));
    }
    r::actor_base_t::shutdown_finish();
}

void initiator_actor_t::initiate(transport::stream_sp_t stream, const utils::URI &uri) noexcept {
    transport = std::move(stream);
    LOG_TRACE(log, "{}, resolving {} (transport = {})", identity, uri.full, (void *)transport.get());
    pt::time_duration resolve_timeout = init_timeout / 2;
    auto port = std::to_string(uri.port);
    request<payload::address_request_t>(resolver, uri.host, port).send(resolve_timeout);
    resources->acquire(resource::resolving);
}

void initiator_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    LOG_TRACE(log, "{}, on_resolve", identity);
    resources->release(resource::resolving);
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto &ee = res.payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_resolve error : {}", identity, ee->message());
        return initiate_active();
    }

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::connect); };
    transport->async_connect(addresses, on_connect, on_error);
    resources->acquire(resource::connect);
}

void initiator_actor_t::on_io_error(const sys::error_code &ec, r::plugin::resource_id_t resource) noexcept {
    LOG_TRACE(log, "{}, on_io_error: {}", identity, ec.message());
    resources->release(resource);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "{}, on_io_error: {}", identity, ec.message());
    }
    if (state < r::state_t::SHUTTING_DOWN) {
        if (!connected && active) {
            initiate_active();
        } else {
            connected = false;
            LOG_DEBUG(log, "{}, on_io_error, initiating shutdown...", identity);
            do_shutdown(make_error(ec));
        }
    }
}

void initiator_actor_t::on_connect(resolve_it_t) noexcept {
    LOG_TRACE(log, "{}, on_connect, device_id = {}", identity, peer_device_id.get_short());
    initiate_handshake();
    resources->release(resource::connect);
}

void initiator_actor_t::initiate_handshake() noexcept {
    LOG_TRACE(log, "{}, connected, initializing handshake", identity);
    connected = true;
    transport::handshake_fn_t handshake_fn([&](auto &&...args) { on_handshake(args...); });
    transport::error_fn_t error_fn([&](auto arg) { on_io_error(arg, resource::handshake); });
    transport->async_handshake(handshake_fn, error_fn);
    resources->acquire(resource::handshake);
}

void initiator_actor_t::on_handshake(bool valid_peer, utils::x509_t &cert, const tcp::endpoint &peer_endpoint,
                                     const model::device_id_t *peer_device) noexcept {
    resources->release(resource::handshake);
    if (!peer_device) {
        LOG_WARN(log, "{}, on_handshake,  missing peer device id", identity);
        auto ec = utils::make_error_code(utils::error_code_t::missing_device_id);
        return do_shutdown(make_error(ec));
    }

    auto cert_name = utils::get_common_name(cert);
    if (!cert_name) {
        LOG_WARN(log, "{}, on_handshake, can't get certificate name: {}", identity, cert_name.error().message());
        auto ec = utils::make_error_code(utils::error_code_t::missing_cn);
        return do_shutdown(make_error(ec));
    }
    LOG_TRACE(log, "{}, on_handshake, valid = {}, issued by {}", identity, valid_peer, cert_name.value());
    peer_device_id = *peer_device;
    remote_endpoint = peer_endpoint;
    resources->release(resource::initializing);
}
