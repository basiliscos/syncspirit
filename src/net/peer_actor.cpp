#include "peer_actor.h"
#include "names.h"
#include "../utils/tls.h"
#include "../utils/error_code.h"
#include "../proto/bep_support.h"
#include <spdlog/spdlog.h>
#include <boost/core/demangle.hpp>

using namespace syncspirit::net;
using namespace syncspirit;

namespace {
namespace resource {
r::plugin::resource_id_t resolving = 0;
r::plugin::resource_id_t uris = 1;
r::plugin::resource_id_t io = 2;
r::plugin::resource_id_t io_timer = 3;
r::plugin::resource_id_t tx_timer = 4;
r::plugin::resource_id_t rx_timer = 5;
} // namespace resource
} // namespace

peer_actor_t::peer_actor_t(config_t &config)
    : r::actor_base_t{config}, device_name{config.device_name}, bep_config{config.bep_config},
      coordinator{config.coordinator}, peer_device_id{config.peer_device_id}, uris{config.uris},
      sock(std::move(config.sock)), ssl_pair{*config.ssl_pair} {
    rx_buff.resize(config.bep_config.rx_buff_size);
}

static std::string generate_id(const model::device_id_t *device_id, const tcp::endpoint *remote) noexcept {
    std::string r;
    if (device_id) {
        r += device_id->get_short();
    } else {
        r += "[?]";
    }
    r += "/";
    if (remote) {
        r += remote->address().to_string();
        r += ":";
        r += std::to_string(remote->port());
    } else {
        r += "[?]";
    }
    return r;
}

void peer_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);

    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        sys::error_code ec;
        tcp::endpoint remote;
        if (sock) {
            remote = sock.value().remote_endpoint(ec);
        }
        auto value = generate_id((sock ? nullptr : &peer_device_id), (ec ? nullptr : &remote));
        p.set_identity(value, false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&peer_actor_t::on_resolve);
        p.subscribe_actor(&peer_actor_t::on_auth);
        p.subscribe_actor(&peer_actor_t::on_start_reading);
        instantiate_transport();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::resolver, resolver).link(false); });
}

void peer_actor_t::instantiate_transport() noexcept {
    if (sock) {
        transport::ssl_junction_t ssl{peer_device_id, &ssl_pair, false};
        auto uri = utils::parse("tcp://0.0.0.0/").value();
        auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
        transport::transport_config_t cfg{transport::ssl_option_t(ssl), uri, *sup, std::move(sock)};
        transport = transport::initiate_stream(cfg);
        resources->acquire(resource::io);
        initiate_handshake();
    } else {
        resources->acquire(resource::uris);
        try_next_uri();
    }
}

void peer_actor_t::try_next_uri() noexcept {
    transport::ssl_junction_t ssl{peer_device_id, &ssl_pair, false};
    while (++uri_idx < (std::int32_t)uris.size()) {
        auto &uri = uris[uri_idx];
        auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
        // spdlog::warn("url: {}", uri.full);
        transport::transport_config_t cfg{transport::ssl_option_t(ssl), uri, *sup, {}};
        auto result = transport::initiate_stream(cfg);
        if (result) {
            initiate(std::move(result), uri);
            resources->release(resource::uris);
            return;
        }
    }

    spdlog::trace("{}, try_next_uri, no way to conenct found, shut down", identity);
    resources->release(resource::uris);
    auto ec = utils::make_error_code(utils::error_code::connection_impossible);
    do_shutdown(make_error(ec));
}

void peer_actor_t::initiate(transport::stream_sp_t tran, const utils::URI &url) noexcept {
    transport = std::move(tran);

    spdlog::trace("{}, try_next_uri, will initate connection with via {} (transport = {})", identity, url.full,
                  (void *)transport.get());
    pt::time_duration resolve_timeout = init_timeout / 2;
    auto port = std::to_string(url.port);
    request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
    resources->acquire(resource::resolving);
    return;
}

void peer_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    resources->release(resource::resolving);

    auto &ee = res.payload.ee;
    if (ee) {
        spdlog::warn("{}, on_resolve error : {}", identity, ee->message());
        resources->acquire(resource::uris);
        return try_next_uri();
    }

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_connect(addresses, on_connect, on_error);
    resources->acquire(resource::io);

    auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
    timer_request = start_timer(timeout, *this, &peer_actor_t::on_timer);
    resources->acquire(resource::io_timer);
}

void peer_actor_t::on_connect(resolve_it_t) noexcept {
    spdlog::trace("{}, on_connect, device_id = {}", identity, peer_device_id.get_short());
    initiate_handshake();
}

void peer_actor_t::initiate_handshake() noexcept {
    connected = true;
    transport::handshake_fn_t handshake_fn([&](auto &&...args) { on_handshake(args...); });
    transport::error_fn_t error_fn([&](auto arg) { on_handshake_error(arg); });
    transport->async_handshake(handshake_fn, error_fn);
}

void peer_actor_t::on_io_error(const sys::error_code &ec) noexcept {
    spdlog::trace("{}, on_io_error: {}", identity, ec.message());
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("{}, on_io_error: {}", identity, ec.message());
    }
    cancel_timer();
    if (!connected && state < r::state_t::SHUTTING_DOWN) {
        // transport.reset();
        resources->acquire(resource::uris);
        try_next_uri();
    } else {
        do_shutdown(make_error(ec));
    }
}

void peer_actor_t::process_tx_queue() noexcept {
    assert(!tx_item);
    if (!tx_queue.empty()) {
        auto &item = tx_queue.front();
        tx_item = std::move(item);
        tx_queue.pop_front();
        transport::io_fn_t on_write = [&](auto arg) { this->on_write(arg); };
        transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
        auto &tx_buff = tx_item->buff;

        if (tx_buff.size()) {
            resources->acquire(resource::io);
            transport->async_send(asio::buffer(tx_buff.data(), tx_buff.size()), on_write, on_error);
        } else {
            spdlog::trace("peer_actor_t::process_tx_queue, device_id = {}, final empty message, shutting down ",
                          peer_device_id);
            assert(tx_item->final);
            auto ec = r::make_error_code(r::shutdown_code_t::normal);
            do_shutdown(make_error(ec));
        }
    }
}

void peer_actor_t::push_write(fmt::memory_buffer &&buff, bool final) noexcept {
    tx_item_t item = new confidential::payload::tx_item_t{std::move(buff), final};
    tx_queue.emplace_back(std::move(item));
    process_tx_queue();
}

void peer_actor_t::on_handshake(bool valid_peer, X509 *cert, const tcp::endpoint &peer_endpoint,
                                const model::device_id_t *peer_device) noexcept {
    resources->release(resource::io);
    if (!peer_device) {
        spdlog::warn("{}, on_handshake,  missing peer device id", identity);
        auto ec = utils::make_error_code(utils::error_code::missing_device_id);
        return do_shutdown(make_error(ec));
    }

    auto new_id = generate_id(peer_device, &peer_endpoint);
    spdlog::trace("{} now becomes {}", identity, new_id);
    identity = new_id;

    identity = new_id;
    auto cert_name = utils::get_common_name(cert);
    if (!cert_name) {
        spdlog::warn("{}, on_handshake, can't get certificate name: {}", identity, cert_name.error().message());
        auto ec = utils::make_error_code(utils::error_code::missing_cn);
        return do_shutdown(make_error(ec));
    }
    spdlog::trace("{}, on_handshake, valid = {}, issued by {}", identity, valid_peer, cert_name.value());

    this->cert_name = cert_name.value();
    this->valid_peer = valid_peer;
    this->peer_device_id = *peer_device;
    this->peer_endpoint = peer_endpoint;

    fmt::memory_buffer buff;
    proto::make_hello_message(buff, device_name);
    push_write(std::move(buff), false);

    read_more();
    read_action = [this](auto &&msg) { read_hello(std::move(msg)); };
}

void peer_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("{}, on_handshake_error: {}", identity, ec.message());
    }
}

void peer_actor_t::read_more() noexcept {
    if (rx_idx >= rx_buff.size()) {
        spdlog::warn("{}, read_more, rx buffer limit reached, {}", identity, rx_buff.size());
        auto ec = utils::make_error_code(utils::error_code::rx_limit_reached);
        return do_shutdown(make_error(ec));
    }

    transport::io_fn_t on_read = [&](auto arg) { this->on_read(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    resources->acquire(resource::io);
    auto buff = asio::buffer(rx_buff.data() + rx_idx, rx_buff.size() - rx_idx);
    transport->async_recv(buff, on_read, on_error);
}

void peer_actor_t::on_write(std::size_t sz) noexcept {
    resources->release(resource::io);
    spdlog::trace("{}, on_write, {} bytes", identity, sz);
    assert(tx_item);
    if (tx_item->final) {
        spdlog::trace("{}, process_tx_queue, final message has been sent, shutting down", identity);
        auto ec = r::make_error_code(r::shutdown_code_t::normal);
        do_shutdown(make_error(ec));
    } else {
        tx_item.reset();
        process_tx_queue();
    }
}

void peer_actor_t::on_read(std::size_t bytes) noexcept {
    assert(read_action);
    resources->release(resource::io);
    spdlog::trace("{}, on_read, {} bytes", identity, bytes);
    auto buff = asio::buffer(rx_buff.data(), bytes);
    auto result = proto::parse_bep(buff);
    if (!result) {
        auto &ec = result.error();
        spdlog::warn("{}, on_read, error parsing message: {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
    auto &value = result.value();
    if (!value.consumed) {
        spdlog::trace("{}, on_read, {} :: incomplete message", identity);
        return read_more();
    }

    cancel_timer();
    read_action(std::move(value.message));
    rx_idx += bytes - value.consumed;
    spdlog::trace("{}, on_read,  rx_idx = {} ", identity, rx_idx);
}

void peer_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::io_timer);
    // spdlog::trace("peer_actor_t::on_timer_trigger, peer = {}, cancelled = {}", peer_identity, cancelled);
    if (!cancelled) {
        if (connected) {
            auto ec = r::make_error_code(r::shutdown_code_t::normal);
            do_shutdown(make_error(ec));
        } else {
            transport->cancel();
        }
    }
}

void peer_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    cancel_timer();
    if (resources->has(resource::io)) {
        transport->cancel();
    }
    if (tx_timer_request) {
        r::actor_base_t::cancel_timer(*tx_timer_request);
    }
    if (rx_timer_request) {
        r::actor_base_t::cancel_timer(*rx_timer_request);
    }
}

void peer_actor_t::cancel_timer() noexcept {
    if (resources->has(resource::io_timer)) {
        r::actor_base_t::cancel_timer(*timer_request);
        timer_request.reset();
    }
}

void peer_actor_t::on_auth(message::auth_response_t &res) noexcept {
    auto &cluster = res.payload.res->cluster_config;
    bool ok = (bool)cluster;
    spdlog::trace("{}, on_auth, value = {}", identity, ok);
    if (!ok) {
        spdlog::debug("{}, on_auth, peer has been rejected in authorization, disconnecting");
        auto ec = utils::make_error_code(utils::error_code::non_authorized);
        return do_shutdown(make_error(ec));
    }

    /*
    for (int i = 0; i < cluster->folders_size(); ++i) {
        auto &f = cluster->folders(i);
        for (auto j = 0; j < f.devices_size(); ++j) {
            auto &d = f.devices(j);
            spdlog::debug(">> {}, folder {}, for device {} has index/max_seq = {}/{} ", identity, f.id(), d.id(),
                          d.index_id(), d.max_sequence());
        }
    }
    */

    fmt::memory_buffer buff;
    serialize(buff, *cluster);
    push_write(std::move(buff), false);

    read_action = [this](auto &&msg) { read_cluster_config(std::move(msg)); };
    read_more();
}

void peer_actor_t::on_start_reading(message::start_reading_t &message) noexcept {
    spdlog::trace("{}, on_start_reading", identity);
    controller = message.payload.controller;
    read_action = [this](auto &&msg) { read_controlled(std::move(msg)); };
    read_more();
}

void peer_actor_t::read_hello(proto::message::message_t &&msg) noexcept {
    spdlog::trace("{}, read_hello", identity);
    std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, proto::message::Hello>) {
                spdlog::trace("{}, read_hello,  from {} ({} {})", identity, msg->device_name(), msg->client_name(),
                              msg->client_version());
                request<payload::auth_request_t>(coordinator, get_address(), peer_endpoint, peer_device_id, cert_name,
                                                 std::move(*msg))
                    .send(init_timeout / 2);
            } else {
                spdlog::warn("{}, hello, unexpected_message", identity);
                auto ec = utils::make_error_code(utils::bep_error_code::unexpected_message);
                do_shutdown(make_error(ec));
            }
        },
        msg);
}

void peer_actor_t::read_cluster_config(proto::message::message_t &&msg) noexcept {
    spdlog::trace("{}, read_cluster_config", identity);
    std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, proto::message::ClusterConfig>) {
                proto::ClusterConfig &config = *msg;
                send<payload::connect_notify_t>(supervisor->get_address(), get_address(), peer_device_id,
                                                std::move(config));
                reset_tx_timer();
                reset_rx_timer();
            } else {
                spdlog::warn("{}, read_cluster_config: unexpected_message", identity);
                auto ec = utils::make_error_code(utils::bep_error_code::unexpected_message);
                do_shutdown(make_error(ec));
            }
        },
        msg);
}

void peer_actor_t::read_controlled(proto::message::message_t &&msg) noexcept {
    spdlog::trace("{}, read_controlled", identity);
    std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            namespace m = proto::message;
            spdlog::debug("{}, read_controlled, {}", identity, boost::core::demangle(typeid(T).name()));
            const constexpr bool unexpected = std::is_same_v<T, m::Hello> || std::is_same_v<T, m::ClusterConfig>;
            if constexpr (unexpected) {
                spdlog::warn("{}, hello, unexpected_message", identity);
                auto ec = utils::make_error_code(utils::bep_error_code::unexpected_message);
                do_shutdown(make_error(ec));
            } else if constexpr (std::is_same_v<T, m::Ping>) {
                handle_ping(std::move(msg));
            } else if constexpr (std::is_same_v<T, m::Close>) {
                handle_close(std::move(msg));
            } else {
                auto fwd = payload::forwarded_message_t{std::move(msg)};
                send<payload::forwarded_message_t>(controller, std::move(fwd));
                reset_rx_timer();
            }
        },
        msg);
}

void peer_actor_t::handle_ping(proto::message::Ping &&) noexcept { spdlog::trace("{}, handle_ping", identity); }

void peer_actor_t::handle_close(proto::message::Close &&message) noexcept {
    auto &reason = message->reason();
    const char *str = reason.c_str();
    spdlog::trace("{}, handle_close, reason = {}", identity, reason);
    if (reason.size() == 0) {
        str = "no reason specified";
    }
    auto ee = r::make_error(str, r::shutdown_code_t::normal);
    do_shutdown(ee);
}

void peer_actor_t::reset_tx_timer() noexcept {
    if (state == r::state_t::OPERATIONAL) {
        if (tx_timer_request) {
            r::actor_base_t::cancel_timer(*tx_timer_request);
        }
        auto timeout = pt::milliseconds(bep_config.tx_timeout);
        tx_timer_request = start_timer(timeout, *this, &peer_actor_t::on_tx_timeout);
        resources->acquire(resource::tx_timer);
    }
}

void peer_actor_t::on_tx_timeout(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::tx_timer);
    tx_timer_request.reset();
    if (!cancelled) {
        fmt::memory_buffer buff;
        proto::Ping ping;
        proto::serialize(buff, ping);
        push_write(std::move(buff), false);
        reset_tx_timer();
    }
}

void peer_actor_t::reset_rx_timer() noexcept {
    if (state == r::state_t::OPERATIONAL) {
        if (rx_timer_request) {
            r::actor_base_t::cancel_timer(*rx_timer_request);
        }
        auto timeout = pt::milliseconds(bep_config.rx_timeout);
        rx_timer_request = start_timer(timeout, *this, &peer_actor_t::on_rx_timeout);
        resources->acquire(resource::rx_timer);
    }
}

void peer_actor_t::on_rx_timeout(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::rx_timer);
    rx_timer_request.reset();
    if (!cancelled) {
        auto ec = utils::make_error_code(utils::error_code::rx_timeout);
        auto reason = make_error(ec);
        do_shutdown(reason);
    }
}
