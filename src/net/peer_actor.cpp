// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "peer_actor.h"
#include "names.h"
#include "constants.h"
#include "utils/tls.h"
#include "utils/error_code.h"
#include "proto/bep_support.h"
#include "model/messages.h"
#include "model/diff/peer/peer_state.h"
#include <boost/core/demangle.hpp>

using namespace syncspirit::net;
using namespace syncspirit;

namespace {
namespace resource {
r::plugin::resource_id_t resolving = 0;
r::plugin::resource_id_t uris = 1;
r::plugin::resource_id_t io_handshake = 2;
r::plugin::resource_id_t io_read = 3;
r::plugin::resource_id_t io_write = 4;
r::plugin::resource_id_t io_connect = 5;
r::plugin::resource_id_t io_timer = 6;
r::plugin::resource_id_t tx_timer = 7;
r::plugin::resource_id_t rx_timer = 8;
r::plugin::resource_id_t finalization = 9;
} // namespace resource
} // namespace

peer_actor_t::peer_actor_t(config_t &config)
    : r::actor_base_t{config}, cluster{config.cluster}, device_name{config.device_name}, bep_config{config.bep_config},
      coordinator{config.coordinator}, peer_device_id{config.peer_device_id}, uris{config.uris},
      sock(std::move(config.sock)), ssl_pair{*config.ssl_pair} {
    rx_buff.resize(config.bep_config.rx_buff_size);
    log = utils::get_logger("net.peer_actor");
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
        p.subscribe_actor(&peer_actor_t::on_start_reading);
        p.subscribe_actor(&peer_actor_t::on_termination);
        p.subscribe_actor(&peer_actor_t::on_block_request);
        p.subscribe_actor(&peer_actor_t::on_forward);
        instantiate_transport();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::resolver, resolver).link(false); });
}

void peer_actor_t::instantiate_transport() noexcept {
    if (sock) {
        transport::ssl_junction_t ssl{peer_device_id, &ssl_pair, false, ""};
        auto uri = utils::parse("tcp://0.0.0.0/").value();
        auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
        transport::transport_config_t cfg{transport::ssl_option_t(ssl), uri, *sup, std::move(sock)};
        transport = transport::initiate_stream(cfg);
        auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
        timer_request = start_timer(timeout, *this, &peer_actor_t::on_timer);
        resources->acquire(resource::io_timer);
        initiate_handshake();
    } else {
        resources->acquire(resource::uris);
        try_next_uri();
    }
}

void peer_actor_t::try_next_uri() noexcept {
    transport::ssl_junction_t ssl{peer_device_id, &ssl_pair, false, constants::protocol_name};
    while (++uri_idx < (std::int32_t)uris.size()) {
        auto &uri = uris[uri_idx];
        auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
        // log->warn("url: {}", uri.full);
        transport::transport_config_t cfg{transport::ssl_option_t(ssl), uri, *sup, {}};
        auto result = transport::initiate_stream(cfg);
        if (result) {
            initiate(std::move(result), uri);
            resources->release(resource::uris);
            return;
        }
    }

    LOG_TRACE(log, "{}, try_next_uri, no way to conenct found, shut down", identity);
    resources->release(resource::uris);
    auto ec = utils::make_error_code(utils::error_code_t::connection_impossible);
    do_shutdown(make_error(ec));
}

void peer_actor_t::initiate(transport::stream_sp_t tran, const utils::URI &url) noexcept {
    transport = std::move(tran);

    LOG_TRACE(log, "{}, try_next_uri, will initate connection with via {} (transport = {})", identity, url.full,
              (void *)transport.get());
    pt::time_duration resolve_timeout = init_timeout / 2;
    auto port = std::to_string(url.port);
    request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
    resources->acquire(resource::resolving);
    return;
}

void peer_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    resources->release(resource::resolving);
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto &ee = res.payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_resolve error : {}", identity, ee->message());
        resources->acquire(resource::uris);
        return try_next_uri();
    }

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::io_connect); };
    transport->async_connect(addresses, on_connect, on_error);
    resources->acquire(resource::io_connect);

    auto timeout = r::pt::milliseconds{bep_config.connect_timeout};
    timer_request = start_timer(timeout, *this, &peer_actor_t::on_timer);
    resources->acquire(resource::io_timer);
}

void peer_actor_t::on_connect(resolve_it_t) noexcept {
    LOG_TRACE(log, "{}, on_connect, device_id = {}", identity, peer_device_id.get_short());
    initiate_handshake();
    resources->release(resource::io_connect);
}

void peer_actor_t::initiate_handshake() noexcept {
    connected = true;
    transport::handshake_fn_t handshake_fn([&](auto &&...args) { on_handshake(args...); });
    transport::error_fn_t error_fn([&](auto arg) { on_io_error(arg, resource::io_handshake); });
    transport->async_handshake(handshake_fn, error_fn);
    resources->acquire(resource::io_handshake);
}

void peer_actor_t::on_io_error(const sys::error_code &ec, rotor::plugin::resource_id_t resource) noexcept {
    LOG_TRACE(log, "{}, on_io_error: {}", identity, ec.message());
    resources->release(resource);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "{}, on_io_error: {}", identity, ec.message());
    }
    cancel_timer();
    cancel_io();
    if (resources->has(resource::finalization)) {
        resources->release(resource::finalization);
    }
    io_error = true;
    if (state < r::state_t::SHUTTING_DOWN) {
        if (!connected) {
            resources->acquire(resource::uris);
            try_next_uri();
        } else {
            connected = false;
            LOG_DEBUG(log, "{}, on_io_error, initiating shutdown...", identity);
            do_shutdown(make_error(ec));
        }
    }
}

void peer_actor_t::process_tx_queue() noexcept {
    if (finished) {
        return;
    }
    assert(!tx_item);
    if (!tx_queue.empty() && !finished) {
        auto &item = tx_queue.front();
        tx_item = std::move(item);
        tx_queue.pop_front();
        transport::io_fn_t on_write = [&](auto arg) { this->on_write(arg); };
        transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::io_write); };
        auto &tx_buff = tx_item->buff;

        if (tx_buff.size()) {
            resources->acquire(resource::io_write);
            transport->async_send(asio::buffer(tx_buff.data(), tx_buff.size()), on_write, on_error);
        } else {
            LOG_TRACE(log, "peer_actor_t::process_tx_queue, device_id = {}, final empty message, shutting down ",
                      peer_device_id);
            assert(tx_item->final);
            auto ec = r::make_error_code(r::shutdown_code_t::normal);
            do_shutdown(make_error(ec));
            finished = true;
        }
    }
}

void peer_actor_t::push_write(fmt::memory_buffer &&buff, bool final) noexcept {
    if (io_error) {
        return;
    }
    tx_item_t item = new confidential::payload::tx_item_t{std::move(buff), final};
    tx_queue.emplace_back(std::move(item));
    if (!tx_item) {
        process_tx_queue();
    }
    if (final) {
        resources->acquire(resource::finalization);
    }
}

void peer_actor_t::on_handshake(bool valid_peer, utils::x509_t &cert, const tcp::endpoint &peer_endpoint,
                                const model::device_id_t *peer_device) noexcept {
    resources->release(resource::io_handshake);
    handshaked = true;
    if (!peer_device) {
        LOG_WARN(log, "{}, on_handshake,  missing peer device id", identity);
        auto ec = utils::make_error_code(utils::error_code_t::missing_device_id);
        return do_shutdown(make_error(ec));
    }

    auto new_id = generate_id(peer_device, &peer_endpoint);
    LOG_DEBUG(log, "{} now becomes {}", identity, new_id);
    identity = new_id;

    identity = new_id;
    auto cert_name = utils::get_common_name(cert);
    if (!cert_name) {
        LOG_WARN(log, "{}, on_handshake, can't get certificate name: {}", identity, cert_name.error().message());
        auto ec = utils::make_error_code(utils::error_code_t::missing_cn);
        return do_shutdown(make_error(ec));
    }
    LOG_TRACE(log, "{}, on_handshake, valid = {}, issued by {}", identity, valid_peer, cert_name.value());

    this->cert_name = cert_name.value();
    this->valid_peer = valid_peer;
    this->peer_device_id = *peer_device;
    this->peer_endpoint = peer_endpoint;

    fmt::memory_buffer buff;
    proto::make_hello_message(buff, device_name);
    push_write(std::move(buff), false);

    read_more();
    read_action = &peer_actor_t::read_hello;
}

#if 0
void peer_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "{}, on_handshake_error: {}", identity, ec.message());
    }
}
#endif

void peer_actor_t::read_more() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    if (rx_idx >= rx_buff.size()) {
        LOG_WARN(log, "{}, read_more, rx buffer limit reached, {}", identity, rx_buff.size());
        auto ec = utils::make_error_code(utils::error_code_t::rx_limit_reached);
        return do_shutdown(make_error(ec));
    }

    transport::io_fn_t on_read = [&](auto arg) { this->on_read(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::io_read); };
    resources->acquire(resource::io_read);
    auto buff = asio::buffer(rx_buff.data() + rx_idx, rx_buff.size() - rx_idx);
    LOG_TRACE(log, "{}, read_more", identity);
    transport->async_recv(buff, on_read, on_error);
}

void peer_actor_t::on_write(std::size_t sz) noexcept {
    resources->release(resource::io_write);
    LOG_TRACE(log, "{}, on_write, {} bytes", identity, sz);
    assert(tx_item);
    if (tx_item->final) {
        LOG_TRACE(log, "{}, process_tx_queue, final message has been sent, shutting down", identity);
        if (resources->has(resource::finalization)) {
            resources->release(resource::finalization);
        }
        cancel_io();
    } else {
        tx_item.reset();
        process_tx_queue();
    }
}

void peer_actor_t::on_read(std::size_t bytes) noexcept {
    assert(read_action);
    resources->release(resource::io_read);
    rx_idx += bytes;
    LOG_TRACE(log, "{}, on_read, {} bytes, total = {}", identity, bytes, rx_idx);
    auto buff = asio::buffer(rx_buff.data(), rx_idx);
    auto result = proto::parse_bep(buff);
    if (result.has_error()) {
        auto &ec = result.error();
        LOG_WARN(log, "{}, on_read, error parsing message: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &value = result.value();
    if (!value.consumed) {
        // log->trace("{}, on_read :: incomplete message", identity);
        return read_more();
    }

    cancel_timer();
    assert(value.consumed <= rx_idx);
    rx_idx -= value.consumed;
    if (rx_idx) {
        auto ptr = rx_buff.data();
        std::memcpy(ptr, ptr + value.consumed, rx_idx);
    }
    (this->*read_action)(std::move(value.message));
    LOG_TRACE(log, "{}, on_read,  rx_idx = {} ", identity, rx_idx);
}

void peer_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::io_timer);
    LOG_TRACE(log, "{}, on_timer_trigger, cancelled = {}", identity, cancelled);
    if (!cancelled) {
        cancel_io();
        if (connected) {
            auto ec = r::make_error_code(r::shutdown_code_t::normal);
            do_shutdown(make_error(ec));
        }
    }
}

void peer_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    if (resources->has(resource::io_timer)) {
        cancel_timer();
    }
    if (tx_timer_request) {
        r::actor_base_t::cancel_timer(*tx_timer_request);
    }
    if (rx_timer_request) {
        r::actor_base_t::cancel_timer(*rx_timer_request);
    }
    if (controller) {
        send<payload::termination_t>(controller, shutdown_reason);
    }

    if (handshaked) {
        fmt::memory_buffer buff;
        proto::Close close;
        close.set_reason(shutdown_reason->message());
        proto::serialize(buff, close);
        tx_queue.clear();
        push_write(std::move(buff), true);
        LOG_TRACE(log, "{}, going to send close message", identity);
    } else {
        cancel_io();
    }

    r::actor_base_t::shutdown_start();
}

void peer_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    for (auto &it : block_requests) {
        auto ec = r::make_error_code(r::error_code_t::cancelled);
        reply_with_error(*it, make_error(ec));
    }
    block_requests.clear();
    if (controller) {
        send<payload::termination_t>(controller, shutdown_reason);
    }
    r::actor_base_t::shutdown_finish();
    if (handshaked) {
        auto sha256 = peer_device_id.get_sha256();
        auto device = cluster->get_devices().by_sha256(sha256);
        if (device && device->is_online()) {
            auto diff = model::diff::cluster_diff_ptr_t();
            diff = new model::diff::peer::peer_state_t(*cluster, sha256, address, false);
            send<model::payload::model_update_t>(coordinator, std::move(diff));
        }
    }
}

void peer_actor_t::cancel_timer() noexcept {
    if (resources->has(resource::io_timer)) {
        r::actor_base_t::cancel_timer(*timer_request);
        timer_request.reset();
    }
}

void peer_actor_t::cancel_io() noexcept {
    if (resources->has(resource::io_read)) {
        LOG_TRACE(log, "{}, cancelling I/O (read)", identity);
        transport->cancel();
        return;
    }
    if (resources->has(resource::io_write)) {
        LOG_TRACE(log, "{}, cancelling I/O (write)", identity);
        transport->cancel();
        return;
    }
    if (resources->has(resource::io_connect)) {
        LOG_TRACE(log, "{}, cancelling I/O (connect)", identity);
        transport->cancel();
        return;
    }
    if (resources->has(resource::io_handshake)) {
        LOG_TRACE(log, "{}, cancelling I/O (handshake)", identity);
        transport->cancel();
        return;
    }
}

void peer_actor_t::on_start_reading(message::start_reading_t &message) noexcept {
    bool start = message.payload.start;
    LOG_TRACE(log, "{}, on_start_reading, start = ", identity, start);
    controller = message.payload.controller;
    if (start) {
        read_action = &peer_actor_t::read_controlled;
        read_more();
    }
}

void peer_actor_t::on_termination(message::termination_signal_t &message) noexcept {
    if (!shutdown_reason) {
        auto &ee = message.payload.ee;
        auto reason = ee->message();
        LOG_TRACE(log, "{}, on_termination: {}", identity, reason);
        do_shutdown(ee);
    }
}

void peer_actor_t::on_block_request(message::block_request_t &message) noexcept {
    auto req_id = (std::int32_t)message.payload.id;
    proto::Request req;
    auto &p = message.payload.request_payload;
    auto &file = p.file;
    auto &file_block = p.block;
    auto &block = *file_block.block();
    req.set_id(req_id);
    *req.mutable_folder() = file->get_folder_info()->get_folder()->get_id();
    *req.mutable_name() = file->get_name();

    req.set_offset(file_block.get_offset());
    req.set_size(block.get_size());
    *req.mutable_hash() = block.get_hash();

    fmt::memory_buffer buff;
    proto::serialize(buff, req);
    push_write(std::move(buff), false);
    block_requests.emplace_back(&message);
}

void peer_actor_t::on_forward(message::forwarded_message_t &message) noexcept {
    LOG_TRACE(log, "{}, on_forward", identity);
    fmt::memory_buffer buff;

    std::visit([&](auto &&msg) { proto::serialize(buff, *msg); }, message.payload);

    push_write(std::move(buff), false);
}

void peer_actor_t::read_hello(proto::message::message_t &&msg) noexcept {
    using namespace model::diff;
    LOG_TRACE(log, "{}, read_hello", identity);
    std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, proto::message::Hello>) {
                LOG_TRACE(log, "{}, read_hello, from {} ({} {})", identity, msg->device_name(), msg->client_name(),
                          msg->client_version());
                auto peer = cluster->get_devices().by_sha256(peer_device_id.get_sha256());
                if (peer && peer->is_online()) {
                    auto ec = utils::make_error_code(utils::error_code_t::already_connected);
                    return do_shutdown(make_error(ec));
                }
                auto diff = cluster_diff_ptr_t();
                diff = new peer::peer_state_t(*cluster, peer_device_id.get_sha256(), get_address(), true, cert_name,
                                              peer_endpoint, msg->client_name());
                send<model::payload::model_update_t>(coordinator, std::move(diff));
            } else {
                LOG_WARN(log, "{}, read_hello, unexpected_message", identity);
                auto ec = utils::make_error_code(utils::bep_error_code_t::unexpected_message);
                return do_shutdown(make_error(ec));
            }
        },
        msg);
}

void peer_actor_t::read_controlled(proto::message::message_t &&msg) noexcept {
    LOG_TRACE(log, "{}, read_controlled", identity);
    bool continue_reading = true;
    std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            using boost::core::demangle;
            namespace m = proto::message;
            LOG_DEBUG(log, "{}, read_controlled, {}", identity, demangle(typeid(T).name()));
            const constexpr bool unexpected = std::is_same_v<T, m::Hello>;
            if constexpr (unexpected) {
                LOG_WARN(log, "{}, hello, unexpected_message", identity);
                auto ec = utils::make_error_code(utils::bep_error_code_t::unexpected_message);
                do_shutdown(make_error(ec));
            } else if constexpr (std::is_same_v<T, m::Ping>) {
                handle_ping(std::move(msg));
            } else if constexpr (std::is_same_v<T, m::Close>) {
                handle_close(std::move(msg));
                continue_reading = false;
            } else if constexpr (std::is_same_v<T, m::Response>) {
                handle_response(std::move(msg));
            } else {
                auto fwd = payload::forwarded_message_t{std::move(msg)};
                send<payload::forwarded_message_t>(controller, std::move(fwd));
                reset_rx_timer();
            }
        },
        msg);
    if (continue_reading) {
        read_action = &peer_actor_t::read_controlled;
        read_more();
    }
}

void peer_actor_t::handle_ping(proto::message::Ping &&) noexcept { log->trace("{}, handle_ping", identity); }

void peer_actor_t::handle_close(proto::message::Close &&message) noexcept {
    auto &reason = message->reason();
    const char *str = reason.c_str();
    LOG_TRACE(log, "{}, handle_close, reason = {}", identity, reason);
    if (reason.size() == 0) {
        str = "no reason specified";
    }
    auto ee = r::make_error(str, r::shutdown_code_t::normal);
    do_shutdown(ee);
}

void peer_actor_t::handle_response(proto::message::Response &&message) noexcept {
    auto id = message->id();
    LOG_TRACE(log, "{}, handle_response, message id = {}", identity, id);
    auto predicate = [id = id](const block_request_ptr_t &it) { return ((std::int32_t)it->payload.id) == id; };
    auto it = std::find_if(block_requests.begin(), block_requests.end(), predicate);
    if (it == block_requests.end()) {
        if (!shutdown_reason) {
            LOG_WARN(log, "{}, response for unexpected request id {}", identity, id);
            auto ec = utils::make_error_code(utils::bep_error_code_t::response_mismatch);
            do_shutdown(make_error(ec));
        }
    }

    auto error = message->code();
    auto &block_request = *it;
    if (!shutdown_reason) {
        if (error) {
            auto ec = utils::make_error_code((utils::request_error_code_t)error);
            LOG_WARN(log, "{}, block request error: {}", identity, ec.message());
            reply_with_error(*block_request, make_error(ec));
        } else {
            auto &data = message->data();
            auto request_sz = block_request->payload.request_payload.block.block()->get_size();
            if (data.size() != request_sz) {
                LOG_WARN(log, "{}, got {} bytes, but requested {}", identity, data.size(), request_sz);
                auto ec = utils::make_error_code(utils::bep_error_code_t::response_missize);
                return do_shutdown(make_error(ec));
            }
            reply_to(*block_request, std::move(data));
        }
    }
    block_requests.erase(it);
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
        auto ec = utils::make_error_code(utils::error_code_t::rx_timeout);
        auto reason = make_error(ec);
        do_shutdown(reason);
    }
}
