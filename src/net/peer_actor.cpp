#include "peer_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t resolving = 0;
r::plugin::resource_id_t uris = 1;
r::plugin::resource_id_t io = 2;
r::plugin::resource_id_t timer = 3;
} // namespace resource
} // namespace

peer_actor_t::peer_actor_t(config_t &config)
    : r::actor_base_t{config}, device_id{config.peer_device_id},
      device_name{config.device_name}, uris{config.uris}, ssl_pair{*config.ssl_pair} {
    rx_buff.resize(config.bep_config.rx_buff_size);
}

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
    while (uri_idx < (std::int32_t)uris.size()) {
        auto &uri = uris[++uri_idx];
        auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
        // spdlog::warn("url: {}", uri.full);
        transport::transport_config_t cfg{transport::ssl_option_t(ssl), uri, *sup};
        auto result = transport::initiate(cfg);
        if (result) {
            initiate(std::move(result), uri);
            resources->release(resource::uris);
            return;
        }
    }

    spdlog::trace("peer_actor_t::try_next_uri, no way to conenct with {} found, shut down", device_id);
    resources->release(resource::uris);
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
        spdlog::warn("peer_actor_t::on_resolve error, {} : {}", device_id, ec.message());
        return try_next_uri();
    }

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_connect(addresses, on_connect, on_error);
    resources->acquire(resource::io);

    pt::time_duration timeout = init_timeout * 8 / 10;
    timer_request = start_timer(timeout, *this, &peer_actor_t::on_timer);
    resources->acquire(resource::timer);
}

void peer_actor_t::on_connect(resolve_it_t) noexcept {
    spdlog::trace("peer_actor_t::on_connect(), device_id = {}", device_id);

    transport::handshake_fn_t handshake_fn([&](auto arg, auto peer_cert) { on_handshake(arg, peer_cert); });
    transport::error_fn_t error_fn([&](auto arg) { on_handshake_error(arg); });
    transport->async_handshake(handshake_fn, error_fn);
}

void peer_actor_t::on_io_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("peer_actor_t::on_io_error, {} :: {}", device_id, ec.message());
    }
    cancel_timer();
    if (resources->has(resource::uris)) {
        try_next_uri();
    } else {
        do_shutdown();
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
            assert(tx_item->final);
            spdlog::trace("peer_actor_t::process_tx_queue, device_id = {}, final empty message, shutting down ",
                          device_id);
            do_shutdown();
        }
    }
}

void peer_actor_t::push_write(fmt::memory_buffer &&buff, bool final) noexcept {
    tx_item_t item = new confidential::payload::tx_item_t{std::move(buff), final};
    tx_queue.emplace_back(std::move(item));
    process_tx_queue();
}

void peer_actor_t::on_handshake(bool valid_peer, X509 *) noexcept {
    resources->release(resource::io);
    this->valid_peer = valid_peer;
    spdlog::trace("peer_actor_t::on_handshake, device_id = {}, valid = {} ", device_id, valid_peer);

    fmt::memory_buffer buff;
    proto::make_hello_message(buff, device_name);
    push_write(std::move(buff), false);

    read_more();
    read_action = [this](auto &&msg) { read_hello(std::move(msg)); };
}

void peer_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("peer_actor_t::on_handshake_error, {} :: {}", device_id, ec.message());
    }
}

void peer_actor_t::read_more() noexcept {
    if (rx_idx >= rx_buff.size()) {
        spdlog::warn("peer_actor_t::read_more, {} :: rx buffer limit reached, {}", device_id, rx_buff.size());
        return do_shutdown();
    }

    transport::io_fn_t on_read = [&](auto arg) { this->on_read(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    resources->acquire(resource::io);
    auto buff = asio::buffer(rx_buff.data() + rx_idx, rx_buff.size() - rx_idx);
    transport->async_recv(buff, on_read, on_error);
}

void peer_actor_t::on_write(std::size_t sz) noexcept {
    resources->release(resource::io);
    spdlog::trace("peer_actor_t::on_write, {} :: {} bytes", device_id, sz);
    assert(tx_item);
    if (tx_item->final) {
        spdlog::trace("peer_actor_t::process_tx_queue, device_id = {}, final message has been sent, shutting down ",
                      device_id);
        do_shutdown();
    } else {
        tx_item.reset();
        process_tx_queue();
    }
}

void peer_actor_t::on_read(std::size_t bytes) noexcept {
    assert(read_action);
    resources->release(resource::io);
    spdlog::trace("peer_actor_t::on_read, {} :: {} bytes", device_id, bytes);
    auto buff = asio::buffer(rx_buff.data(), bytes);
    auto result = proto::parse_bep(buff);
    if (!result) {
        spdlog::warn("peer_actor_t::on_read, {} error parsing message:: {}", device_id, result.error().message());
        do_shutdown();
    }
    auto &value = result.value();
    if (!value.consumed) {
        spdlog::trace("peer_actor_t::on_read, {} :: incomplete message", device_id);
        return read_more();
    }

    cancel_timer();
    read_action(std::move(value.message));
    rx_idx += bytes - value.consumed;
    spdlog::trace("peer_actor_t::on_read, {}, rx_idx = {} ", device_id, rx_idx);
}

void peer_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    // spdlog::trace("peer_actor_t::on_timer_trigger, device_id = {}, cancelled = {}", device_id, cancelled);
    if (!cancelled) {
        do_shutdown();
    }
}

void peer_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    cancel_timer();
    if (resources->has(resource::io)) {
        transport->cancel();
    }
}

void peer_actor_t::cancel_timer() noexcept {
    if (resources->has(resource::timer)) {
        r::actor_base_t::cancel_timer(*timer_request);
        timer_request.reset();
    }
}

void peer_actor_t::read_hello(proto::message::message_t &&msg) noexcept {
    spdlog::trace("peer_actor_t::read_hello, device_id = {}", device_id);
    bool ok = std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, proto::message::Hello>) {
                spdlog::info("peer_actor_t::on_read, {} hello from {} ({} {})", device_id, msg->device_name(),
                             msg->client_name(), msg->client_version());
                return true;
            } else {
                spdlog::warn("peer_actor_t::on_read, {} :: unexpected_message", device_id);
                do_shutdown();
                return false;
            }
        },
        msg);
    if (ok) {
        // authorize?
        if (!valid_peer) {
            spdlog::info("peer_actor_t::authorize, {} :: non-valid peer", device_id);
            return do_shutdown();
        } else {
            read_action = [this](auto &&msg) { read_cluster_config(std::move(msg)); };
            read_more();
        }
    }
}

void peer_actor_t::read_cluster_config(proto::message::message_t &&msg) noexcept {
    spdlog::trace("peer_actor_t::read_cluster_config, device_id = {}", device_id);
    std::visit(
        [&](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, proto::message::ClusterConfig>) {
                proto::ClusterConfig &config = *msg;
                /*
                for (int i = 0; i < config.folders_size(); ++i) {
                    auto &f = config.folders(i);
                    printf("folder : %s/%s\n", f.label().c_str(), f.id().c_str());
                }
                auto& m = *msg;
                for(size_t i = 0; i < m.folders_size())
                spdlog::info("peer_actor_t::on_read, {} hello from {} ({} {})", device_id, msg->device_name(),
                             msg->client_name(), msg->client_version());
                */
                send<payload::connect_notify_t>(supervisor->get_address(), get_address(), std::move(config));
            } else {
                spdlog::warn("peer_actor_t::read_cluster_config, {} :: unexpected_message", device_id);
                do_shutdown();
            }
        },
        msg);
}
