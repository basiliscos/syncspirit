// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "initiator_actor.h"
#include "constants.h"
#include "names.h"
#include "model/messages.h"
#include "proto/relay_support.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "model/diff/contact/peer_state.h"
#include <algorithm>
#include <spdlog/fmt/bin_to_hex.h>
#include <fmt/core.h>
#include <ares.h>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t initializing = 0;
r::plugin::resource_id_t resolving = 1;
r::plugin::resource_id_t connect = 2;
r::plugin::resource_id_t handshake = 3;
r::plugin::resource_id_t read = 4;
r::plugin::resource_id_t write = 5;
} // namespace resource
} // namespace

static constexpr size_t BUFF_SZ = 256;

initiator_actor_t::initiator_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, peer_device_id{cfg.peer_device_id}, relay_key(std::move(cfg.relay_session)),
      ssl_pair{*cfg.ssl_pair}, sock(std::move(cfg.sock)), cluster{std::move(cfg.cluster)}, sink(std::move(cfg.sink)),
      custom(std::move(cfg.custom)), router{*cfg.router}, alpn(cfg.alpn) {
    auto tmp_identity = "net.init/unknown";
    auto log = utils::get_logger(tmp_identity);
    for (auto &uri : cfg.uris) {
        if (uri->scheme() != "tcp" && uri->scheme() != "relay") {
            LOG_DEBUG(log, "unsupported scheme in the url '{}'", uri);
        } else {
            if (uri->scheme() == "relay" && !cfg.relay_enabled) {
                LOG_DEBUG(log, "'relay' is not enabled, skipping '{}'", uri);
            } else {
                uris.emplace_back(std::move(uri));
            }
        }
    }
    auto comparator = [](const utils::uri_ptr_t &a, const utils::uri_ptr_t &b) noexcept -> bool {
        if (a->scheme() == b->scheme()) {
            auto x = a->buffer();
            auto y = b->buffer();
            return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
        }
        if (a->scheme() == "relay") {
            return true;
        }
        return false;
    };
    std::sort(uris.begin(), uris.end(), comparator);

    if (sock.has_value()) {
        role = role_t::passive;
    } else {
        if (!relay_key.empty()) {
            role = role_t::relay_passive;
        } else {
            role = role_t::active;
        }
    }
}

void initiator_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);

    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string value;
        switch (role) {
        case role_t::active:
            value = fmt::format("init/active:{}", peer_device_id.get_short());
            break;
        case role_t::passive: {
            auto ep = sock.value().remote_endpoint();
            value = fmt::format("init/passive:{}", ep);
            break;
        }
        case role_t::relay_passive: {
            value = fmt::format("init/relay-passive:{}", peer_device_id.get_short());
            break;
        }
        }
        p.set_identity("net." + value, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&initiator_actor_t::on_resolve);
        resources->acquire(resource::initializing);
        if (role == role_t::active) {
            initiate_active();
        } else if (role == role_t::passive) {
            initiate_passive();
        } else if (role == role_t::relay_passive) {
            initiate_relay_passive();
        }
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::resolver, resolver).link(false);
        p.discover_name(names::coordinator, coordinator).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking && role == role_t::active) {
                auto diff = model::diff::cluster_diff_ptr_t();
                auto state = model::device_state_t::connecting;
                auto sha256 = peer_device_id.get_sha256();
                auto peer = cluster && cluster->get_devices().by_sha256(sha256);
                if (peer) {
                    diff = model::diff::contact::peer_state_t::create(*cluster, sha256, nullptr, state);
                    if (diff) {
                        send<model::payload::model_update_t>(coordinator, std::move(diff));
                    }
                }
            }
        });
    });
}

void initiator_actor_t::initiate_active() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    while (uri_idx < uris.size()) {
        auto &uri = uris[uri_idx++];
        if (uri->scheme() == "tcp") {
            initiate_active_tls(uri);
        } else if (uri->scheme() == "relay") {
            initiate_active_relay(uri);
        } else {
            continue;
        }
        return;
    }

    LOG_TRACE(log, "try_next_uri, no way to connect found, shut down");
    auto ec = utils::make_error_code(utils::error_code_t::connection_impossible);
    do_shutdown(make_error(ec));
}

void initiator_actor_t::initiate_passive() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto sup = static_cast<ra::supervisor_asio_t *>(&router);
    transport = transport::initiate_tls_passive(*sup, ssl_pair, std::move(sock.value()), alpn);
    initiate_handshake();
}

void initiator_actor_t::initiate_relay_passive() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto sup = static_cast<ra::supervisor_asio_t *>(&router);
    auto &uri = uris.at(0);
    transport::transport_config_t cfg{{}, uri, *sup, {}, true};
    transport = transport::initiate_stream(cfg);
    assert(transport);
    resolve(uri);
}

void initiator_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "on_start, alpn = {}", alpn);
    std::string proto;
    if (active_uri) {
        proto = active_uri->scheme();
    } else {
        if (role == role_t::relay_passive) {
            proto = "relay";
        } else if (role == role_t::passive) {
            proto = "tcp";
        }
    }
    send<payload::peer_connected_t>(sink, std::move(transport), peer_device_id, remote_endpoint, std::move(proto),
                                    std::move(custom));
    success = true;
    do_shutdown();
}

void initiator_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    if (resources->has(resource::initializing)) {
        resources->release(resource::initializing);
    }
    bool cancel_transport = resources->has(resource::connect) || resources->has(resource::handshake) ||
                            resources->has(resource::read) || resources->has(resource::write);
    if (cancel_transport) {
        transport->cancel();
    }
    r::actor_base_t::shutdown_start();
}

void initiator_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    bool notify_offline = role == role_t::active && !success && cluster;
    if (notify_offline) {
        auto state = model::device_state_t::offline;
        auto sha256 = peer_device_id.get_sha256();
        if (auto peer = cluster->get_devices().by_sha256(sha256); peer) {
            auto diff = model::diff::contact::peer_state_t::create(*cluster, sha256, nullptr, state);
            if (diff) {
                send<model::payload::model_update_t>(coordinator, std::move(diff));
            }
        }
    }
    r::actor_base_t::shutdown_finish();
}

void initiator_actor_t::resolve(const utils::uri_ptr_t &uri) noexcept {
    LOG_DEBUG(log, "resolving {} (transport = {})", uri, (void *)transport.get());
    pt::time_duration resolve_timeout = init_timeout / 2;
    auto host = uri->host();
    auto port = uri->port_number();
    request<payload::address_request_t>(resolver, host, port).send(resolve_timeout);
    resources->acquire(resource::resolving);
}

void initiator_actor_t::initiate_active_tls(const utils::uri_ptr_t &uri) noexcept {
    LOG_DEBUG(log, "trying '{}' as active tls, alpn = {}", uri, alpn);
    auto sup = static_cast<ra::supervisor_asio_t *>(&router);
    transport = transport::initiate_tls_active(*sup, ssl_pair, peer_device_id, uri, false, alpn);
    active_uri = uri;
    relaying = false;
    resolve(uri);
}

void initiator_actor_t::initiate_active_relay(const utils::uri_ptr_t &uri) noexcept {
    LOG_TRACE(log, "trying '{}' as active relay", uri);
    auto relay_device = proto::relay::parse_device(uri);
    if (!relay_device) {
        LOG_WARN(log, "relay url '{}' does not contains valid device_id", uri);
        return initiate_active();
    }
    active_uri = uri;
    relaying = true;
    auto sup = static_cast<ra::supervisor_asio_t *>(&router);
    transport = transport::initiate_tls_active(*sup, ssl_pair, relay_device.value(), active_uri);
    resolve(uri);
}

void initiator_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    LOG_TRACE(log, "on_resolve");
    resources->release(resource::resolving);
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto &ee = res.payload.ee;
    if (ee) {
        LOG_WARN(log, "on_resolve error : {}", ee->message());
        if (role == role_t::active) {
            return initiate_active();
        } else {
            return do_shutdown(ee);
        }
    }

    auto &ips = res.payload.res->results;
    auto port = res.payload.req->payload.request_payload->port;
    auto endpoints = utils::make_endpoints<tcp, tcp::endpoint>(ips, port);

    transport::connect_fn_t on_connect = [&](const auto &arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::connect); };
    transport->async_connect(std::move(endpoints), on_connect, on_error);
    resources->acquire(resource::connect);
}

void initiator_actor_t::on_io_error(const sys::error_code &ec, r::plugin::resource_id_t resource) noexcept {
    LOG_TRACE(log, "on_io_error: {}", ec.message());
    resources->release(resource);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "on_io_error: {}", ec.message());
    }
    if (state < r::state_t::SHUTTING_DOWN) {
        if (!connected && role == role_t::active) {
            initiate_active();
        } else {
            connected = false;
            LOG_DEBUG(log, "on_io_error, initiating shutdown...");
            do_shutdown(make_error(ec));
        }
    }
}

void initiator_actor_t::on_connect(const tcp::endpoint &) noexcept {
    LOG_TRACE(log, "on_connect, device_id = {}, transport = {}", peer_device_id.get_short(), (void *)transport.get());
    resources->release(resource::connect);
    // auto do_handshake = role == role_t::active;
    auto do_handshake =
        (role == role_t::active) &&
        (active_uri && ((((active_uri->scheme() == "relay") && relaying)) || (active_uri->scheme() == "tcp")));
    if (do_handshake) {
        initiate_handshake();
    } else {
        join_session();
    }
}

void initiator_actor_t::initiate_handshake() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    LOG_TRACE(log, "initializing handshake");
    connected = true;
    transport::handshake_fn_t handshake_fn([&](auto &&...args) { on_handshake(args...); });
    transport::error_fn_t error_fn([&](auto arg) { on_io_error(arg, resource::handshake); });
    resources->acquire(resource::handshake);
    transport->async_handshake(handshake_fn, error_fn);
}

void initiator_actor_t::join_session() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    transport::error_fn_t read_err_fn([&](auto arg) { on_io_error(arg, resource::read); });
    transport::io_fn_t read_fn = [this](size_t bytes) { on_read_relay(bytes); };
    rx_buff.resize(BUFF_SZ);
    transport->async_recv(asio::buffer(rx_buff), read_fn, read_err_fn);
    resources->acquire(resource::read);

    LOG_TRACE(log, "join_session");
    auto msg = proto::relay::join_session_request_t{std::move(relay_key)};
    proto::relay::serialize(msg, relay_tx);
    transport::error_fn_t write_err_fn([&](auto arg) { on_io_error(arg, resource::write); });
    transport::io_fn_t write_fn = [this](size_t bytes) { on_write(bytes); };
    transport->async_send(asio::buffer(relay_tx), write_fn, write_err_fn);
    resources->acquire(resource::write);
}

void initiator_actor_t::on_handshake(bool valid_peer, utils::x509_t &cert, const tcp::endpoint &peer_endpoint,
                                     const model::device_id_t *peer_device) noexcept {
    resources->release(resource::handshake);
    if (!peer_device) {
        LOG_WARN(log, "on_handshake,  missing peer device id");
        auto ec = utils::make_error_code(utils::error_code_t::missing_device_id);
        return do_shutdown(make_error(ec));
    }

    auto cert_name = utils::get_common_name(cert);
    if (!cert_name) {
        LOG_WARN(log, "on_handshake, can't get certificate name: {}", cert_name.error().message());
        auto ec = utils::make_error_code(utils::error_code_t::missing_cn);
        return do_shutdown(make_error(ec));
    }
    LOG_TRACE(log, "on_handshake, valid = {}, issued by {}", valid_peer, cert_name.value());
    if (relaying) {
        request_relay_connection();
    } else {
        peer_device_id = *peer_device;
        remote_endpoint = peer_endpoint;
        if (state <= r::state_t::OPERATIONAL) {
            resources->release(resource::initializing);
        }
    }
}

void initiator_actor_t::on_write(size_t bytes) noexcept {
    LOG_TRACE(log, "on_write, {} bytes", bytes);
    resources->release(resource::write);
}

void initiator_actor_t::on_read_relay(size_t bytes) noexcept {
    LOG_TRACE(log, "on_read_relay, {} bytes", bytes);
    resources->release(resource::read);
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto buff = std::string_view(rx_buff.data(), bytes);
    auto r = proto::relay::parse(buff);
    auto wrapped = std::get_if<proto::relay::wrapped_message_t>(&r);
    if (!wrapped) {
        LOG_WARN(log, "unexpected incoming relay data: {}", spdlog::to_hex(buff.begin(), buff.end()));
        auto ec = utils::make_error_code(utils::error_code_t::relay_failure);
        return do_shutdown(make_error(ec));
    }
    auto reply = std::get_if<proto::relay::response_t>(&wrapped->message);
    if (!reply) {
        LOG_WARN(log, "unexpected relay message: {}", spdlog::to_hex(buff.begin(), buff.end()));
        auto ec = utils::make_error_code(utils::error_code_t::relay_failure);
        return do_shutdown(make_error(ec));
    }
    if (reply->code) {
        LOG_WARN(log, "relay join failure({}): {}", reply->code, reply->details);
        auto ec = utils::make_error_code(utils::error_code_t::relay_failure);
        return do_shutdown(make_error(ec));
    }
    auto &upgradeable = dynamic_cast<transport::upgradeable_stream_base_t &>(*transport.get());
    auto ssl = transport::ssl_junction_t{peer_device_id, &ssl_pair, true, constants::protocol_name};
    auto active = role == role_t::active;
    transport = upgradeable.upgrade(ssl, active);
    initiate_handshake();
}

void initiator_actor_t::request_relay_connection() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    transport::error_fn_t read_err_fn([&](auto arg) { on_io_error(arg, resource::read); });
    transport::io_fn_t read_fn = [this](size_t bytes) { on_read_relay_active(bytes); };
    rx_buff.resize(BUFF_SZ);
    transport->async_recv(asio::buffer(rx_buff), read_fn, read_err_fn);
    resources->acquire(resource::read);

    auto sha256 = peer_device_id.get_sha256();
    auto msg = proto::relay::connect_request_t(utils::bytes_t(sha256.begin(), sha256.end()));
    proto::relay::serialize(msg, relay_tx);
    transport::error_fn_t write_err_fn([&](auto arg) { on_io_error(arg, resource::write); });
    transport::io_fn_t write_fn = [this](size_t bytes) { on_write(bytes); };
    transport->async_send(asio::buffer(relay_tx), write_fn, write_err_fn);
    resources->acquire(resource::write);
}

void initiator_actor_t::on_read_relay_active(size_t bytes) noexcept {
    LOG_TRACE(log, "on_read_relay_active, {} bytes", bytes);
    resources->release(resource::read);
    auto buff = std::string_view(rx_buff.data(), bytes);
    auto r = proto::relay::parse(buff);
    auto wrapped = std::get_if<proto::relay::wrapped_message_t>(&r);
    if (!wrapped) {
        LOG_WARN(log, "unexpected incoming relay data: {}", spdlog::to_hex(buff.begin(), buff.end()));
        auto ec = utils::make_error_code(utils::error_code_t::relay_failure);
        return do_shutdown(make_error(ec));
    }
    auto inv = std::get_if<proto::relay::session_invitation_t>(&wrapped->message);
    if (!inv) {
        auto reply = std::get_if<proto::relay::response_t>(&wrapped->message);
        if (reply) {
            LOG_DEBUG(log, "reply (expected session invitation) ({}) : {}", reply->code, reply->details);
        } else {
            LOG_WARN(log, "unexpected relay message: {}", spdlog::to_hex(buff.begin(), buff.end()));
        }
        return initiate_active();
    }
    auto &peer = inv->from;
    if (peer != peer_device_id.get_sha256()) {
        LOG_WARN(log, "unexpected peer device: {}", spdlog::to_hex(peer.begin(), peer.end()));
        return initiate_active();
    }
    auto &addr = inv->address;
    char addr_buff[INET_ADDRSTRLEN + 1] = {0};
    using string_t = decltype(active_uri->host());
    auto host = string_t{};
    auto ip = std::string_view();
    if (addr.has_value()) {
        auto raw_value = addr.value().to_uint();
        ares_inet_ntop(AF_INET, &raw_value, addr_buff, INET_ADDRSTRLEN);
        ip = addr_buff;
    } else {
        host = active_uri->host();
        ip = host;
    }

    relay_key = inv->key;
    auto uri_str = fmt::format("tcp://{}:{}", ip, inv->port);
    LOG_DEBUG(log, "going to connect to {}, using key: {}", uri_str,
              spdlog::to_hex(relay_key.begin(), relay_key.end()));
    auto uri = utils::parse(uri_str);
    relaying = false;

    auto sup = static_cast<ra::supervisor_asio_t *>(&router);
    transport::transport_config_t cfg{{}, uri, *sup, {}, true};
    transport = transport::initiate_stream(cfg);
    resolve(uri);
}
