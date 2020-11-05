#include "http_actor.h"
#include "../utils/error_code.h"
#include "spdlog/spdlog.h"
#include "names.h"

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t request_timer = 1;
r::plugin::resource_id_t connection = 2;
} // namespace resource
} // namespace

http_actor_t::http_actor_t(config_t &config)
    : r::actor_base_t{config}, resolve_timeout(config.resolve_timeout),
      request_timeout(config.request_timeout), registry_name{config.registry_name}, keep_alive{config.keep_alive} {}

void http_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&http_actor_t::on_request);
        p.subscribe_actor(&http_actor_t::on_resolve);
        p.subscribe_actor(&http_actor_t::on_cancel);
        p.subscribe_actor(&http_actor_t::on_close_connection);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(registry_name, get_address());
        p.discover_name(names::resolver, resolver).link(false);
    });
}

void http_actor_t::on_request(message::http_request_t &req) noexcept {
    queue.emplace_back(&req);
    process();
}

void http_actor_t::on_cancel(message::http_cancel_t &req) noexcept {
    if (queue.empty()) {
        return;
    }

    auto &request_id = req.payload.id;
    if (request_id == queue.front()->payload.id) {
        if (resolve_request) {
            send<message::resolve_cancel_t::payload_t>(resolver, *resolve_request, get_address());
        } else {
            cancel_io();
        }
    } else {
        auto it = queue.begin();
        ++it;
        for (; it != queue.end(); ++it) {
            auto &http_req = **it;
            if (http_req.payload.id == request_id) {
                auto ec = r::make_error_code(r::error_code_t::cancelled);
                reply_with_error(http_req, ec);
                queue.erase(it);
                return;
            }
        }
    }
}

void http_actor_t::process() noexcept {
    if (stop_io) {
        auto ec = utils::make_error_code(utils::error_code::service_not_available);
        for (auto req : queue) {
            reply_with_error(*req, ec);
        }
        queue.clear();
        if (resources->has(resource::connection)) {
            cancel_sock();
        }
        return;
    }

    auto skip = queue.empty() || resources->has(resource::io) || resources->has(resource::request_timer);
    if (skip)
        return;

    http_response.clear();
    need_response = true;
    response_size = 0;
    auto &url = queue.front()->payload.request_payload->url;

    if (keep_alive && resources->has(resource::connection)) {
        resources->release(resource::connection);
        if (url.host == resolved_url.host && url.port == resolved_url.port) {
            spdlog::trace("http_actor_t ({}) reusing connection", registry_name);
            spawn_timer();
            write_request();
        } else {
            spdlog::warn("http_actor_t ({}) :: different endpoint is used: {}:{} vs {}:{}", registry_name,
                         resolved_url.host, resolved_url.port, url.host, url.port);
            cancel_sock();
        }
    } else {
        auto port = std::to_string(url.port);
        resolve_request = request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
    }
}

void http_actor_t::spawn_timer() noexcept {
    resources->acquire(resource::io);

    timer_request = start_timer(request_timeout, *this, &http_actor_t::on_timer);
    resources->acquire(resource::request_timer);
}

void http_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    resolve_request.reset();
    auto &ec = res.payload.ec;
    if (ec) {
        spdlog::warn("http_actor_t::on_resolve error: {} ({})", ec.message(), ec.category().name());
        reply_with_error(*queue.front(), ec);
        queue.pop_front();
        need_response = false;
        return process();
    }

    if (stop_io)
        return process();

    auto &payload = queue.front()->payload.request_payload;
    auto &ssl_ctx = payload->ssl_context;
    auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
    transport::transport_config_t cfg{std::move(ssl_ctx), payload->url, *sup};
    transport = transport::initiate(cfg);
    if (!transport) {
        auto ec = utils::make_error_code(utils::error_code::transport_not_available);
        reply_with_error(*queue.front(), ec);
        queue.pop_front();
        need_response = false;
        return process();
    }
    http_adapter = dynamic_cast<transport::http_base_t *>(transport.get());
    assert(http_adapter);

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_connect(addresses, on_connect, on_error);
    spawn_timer();
    resolved_url = payload->url;
}

void http_actor_t::on_connect(resolve_it_t) noexcept {
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }

    transport::handshake_fn_t handshake_fn([&](auto arg, auto peer) { on_handshake(arg, peer); });
    transport::error_fn_t error_fn([&](auto arg) { on_handshake_error(arg); });
    transport->async_handshake(handshake_fn, error_fn);
}

void http_actor_t::write_request() noexcept {
    auto &payload = *queue.front()->payload.request_payload;
    auto &url = payload.url;
    auto &data = payload.data;
    spdlog::trace("http_actor_t ({}) :: sending {} bytes to {} ", registry_name, data.size(), url.full);
    auto buff = asio::buffer(data.data(), data.size());
    transport::io_fn_t on_write = [&](auto arg) { this->on_request_sent(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_send(buff, on_write, on_error);
}

void http_actor_t::on_request_sent(std::size_t /* bytes */) noexcept {
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }

    auto &payload = *queue.front()->payload.request_payload;
    auto &rx_buff = payload.rx_buff;
    rx_buff->prepare(payload.rx_buff_size);
    transport::io_fn_t on_read = [&](auto arg) { this->on_request_read(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    http_adapter->async_read(*rx_buff, http_response, on_read, on_error);
}

void http_actor_t::on_request_read(std::size_t bytes) noexcept {
    response_size = bytes;

    /*
    auto &rx_buff = *queue.front()->payload.request_payload->rx_buff;
    std::string data{(const char *)rx_buff.data().data(), bytes};
    spdlog::debug("http_actor_t::on_request_read ({}): \n{}", registry_name, data);
    */

    if (keep_alive && http_response.keep_alive()) {
        resources->acquire(resource::connection);
    } else {
        transport.reset();
        http_adapter = nullptr;
    }

    resources->release(resource::io);
    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }
    process();
}

void http_actor_t::on_io_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (resources->has(resource::connection)) {
        resources->release(resource::connection);
    }
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_io_error :: {}", ec.message());
    }
    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }
    if (!need_response || stop_io) {
        return process();
    }

    reply_with_error(*queue.front(), ec);
    queue.pop_front();
    need_response = false;
}

void http_actor_t::on_handshake(bool valid_peer, X509 *) noexcept {
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }
    write_request();
}

void http_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_handshake_error :: {}", ec.message());
    }
    if (!need_response || stop_io) {
        return process();
    }
    reply_with_error(*queue.front(), ec);
    queue.pop_front();
    need_response = false;
    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }
    process();
}

void http_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::request_timer);
    timer_request.reset();

    if (!need_response || stop_io) {
        return process();
    }

    if (cancelled) {
        reply_to(*queue.front(), std::move(http_response), response_size);
    } else {
        auto ec = r::make_error_code(r::error_code_t::request_timeout);
        reply_with_error(*queue.front(), ec);
    }
    queue.pop_front();
    need_response = false;

    if (!resources->has(resource::connection)) {
        cancel_io();
    }
    process();
}

void http_actor_t::cancel_sock() noexcept {
    if (resources->has(resource::connection)) {
        resources->release(resource::connection);
    }
    transport.reset();
    http_adapter = nullptr;
}

void http_actor_t::on_start() noexcept {
    spdlog::trace("http_actor_t::on_start({}) (addr = {})", registry_name, (void *)address.get());
    r::actor_base_t::on_start();
}

void http_actor_t::on_close_connection(message::http_close_connection_t &) noexcept {
    if (resources->has(resource::connection)) {
        stop_io = true;
        if (queue.empty()) {
            cancel_sock();
        }
    }
}

void http_actor_t::cancel_io() noexcept {
    if (resources->has(resource::io)) {
        transport->cancel();
    }
    if (resources->has(resource::request_timer)) {
        send<message::resolve_cancel_t::payload_t>(resolver, *resolve_request, get_address());
    }
}
