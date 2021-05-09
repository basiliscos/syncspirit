#include "http_actor.h"
#include "../utils/error_code.h"
#include "spdlog/spdlog.h"
#include "names.h"

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t request_timer = 1;
} // namespace resource
} // namespace

http_actor_t::http_actor_t(config_t &config)
    : r::actor_base_t{config}, resolve_timeout(config.resolve_timeout),
      request_timeout(config.request_timeout), registry_name{config.registry_name}, keep_alive{config.keep_alive} {}

void http_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(registry_name, true); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&http_actor_t::on_request);
        p.subscribe_actor(&http_actor_t::on_resolve);
        p.subscribe_actor(&http_actor_t::on_cancel);
        p.subscribe_actor(&http_actor_t::on_close_connection);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(registry_name, get_address());
        p.discover_name(names::resolver, resolver, true).link(false);
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
                reply_with_error(http_req, make_error(ec));
                queue.erase(it);
                return;
            }
        }
    }
}

void http_actor_t::process() noexcept {
    if (stop_io) {
        auto ec = utils::make_error_code(utils::error_code_t::service_not_available);
        for (auto req : queue) {
            reply_with_error(*req, make_error(ec));
        }
        queue.clear();
        if (resources->has(resource::io)) {
            transport->cancel();
        }
        return;
    }
    auto skip =
        queue.empty() || resources->has(resource::io) || resources->has(resource::request_timer) || resolve_request;
    if (skip)
        return;

    http_response.clear();
    need_response = true;
    response_size = 0;
    auto &url = queue.front()->payload.request_payload->url;

    if (keep_alive && kept_alive) {
        if (url.host == resolved_url.host && url.port == resolved_url.port) {
            spdlog::trace("{} reusing connection", identity);
            spawn_timer();
            write_request();
        } else {
            spdlog::warn("{} :: different endpoint is used: {}:{} vs {}:{}", identity, resolved_url.host,
                         resolved_url.port, url.host, url.port);
            kept_alive = false;
            transport->cancel();
            return;
        }
    } else {
        auto port = std::to_string(url.port);
        resolve_request = request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
    }
}

void http_actor_t::spawn_timer() noexcept {
    assert(!timer_request);
    timer_request = start_timer(request_timeout, *this, &http_actor_t::on_timer);
    resources->acquire(resource::request_timer);
}

void http_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    resolve_request.reset();
    auto &ee = res.payload.ee;
    if (ee) {
        spdlog::warn("{}, on_resolve error: {}", identity, ee->message());
        reply_with_error(*queue.front(), ee);
        queue.pop_front();
        need_response = false;
        return process();
    }

    if (stop_io || queue.empty())
        return process();

    auto &payload = queue.front()->payload.request_payload;
    auto &ssl_ctx = payload->ssl_context;
    auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
    transport::transport_config_t cfg{std::move(ssl_ctx), payload->url, *sup, {}};
    transport = transport::initiate_http(cfg);
    if (!transport) {
        auto ec = utils::make_error_code(utils::error_code_t::transport_not_available);
        reply_with_error(*queue.front(), make_error(ec));
        queue.pop_front();
        need_response = false;
        return process();
    }

    auto &addresses = res.payload.res->results;
    transport::connect_fn_t on_connect = [&](auto arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_connect(addresses, on_connect, on_error);
    resources->acquire(resource::io);
    spawn_timer();
    resolved_url = payload->url;
}

void http_actor_t::on_connect(resolve_it_t) noexcept {
    resources->release(resource::io);
    if (!need_response || stop_io) {
        return process();
    }

    if (queue.front()->payload.request_payload->local_ip) {
        sys::error_code ec;
        local_address = transport->local_address(ec);
        if (ec) {
            spdlog::warn("{}, on_connect, get local addr error :: {}", identity, ec.message());
            reply_with_error(*queue.front(), make_error(ec));
            queue.pop_front();
            need_response = false;
            if (resources->has(resource::request_timer)) {
                cancel_timer(*timer_request);
            }
            return process();
        }
    }

    transport::handshake_fn_t handshake_fn([&](auto &&...args) { on_handshake(args...); });
    transport::error_fn_t error_fn([&](auto arg) { on_handshake_error(arg); });
    transport->async_handshake(handshake_fn, error_fn);
    resources->acquire(resource::io);
}

void http_actor_t::write_request() noexcept {
    auto &payload = *queue.front()->payload.request_payload;
    auto &url = payload.url;
    auto &data = payload.data;
    spdlog::trace("{} :: sending {} bytes to {} ", identity, data.size(), url.full);
    auto buff = asio::buffer(data.data(), data.size());
    transport::io_fn_t on_write = [&](auto arg) { this->on_request_sent(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_send(buff, on_write, on_error);
    resources->acquire(resource::io);
}

void http_actor_t::on_request_sent(std::size_t /* bytes */) noexcept {
    resources->release(resource::io);
    if (!need_response || stop_io) {
        return process();
    }

    auto &payload = *queue.front()->payload.request_payload;
    auto &rx_buff = payload.rx_buff;
    rx_buff->prepare(payload.rx_buff_size);
    transport::io_fn_t on_read = [&](auto arg) { this->on_request_read(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_read(*rx_buff, http_response, on_read, on_error);
    resources->acquire(resource::io);
}

void http_actor_t::on_request_read(std::size_t bytes) noexcept {
    resources->release(resource::io);
    response_size = bytes;

    /*
    auto &rx_buff = *queue.front()->payload.request_payload->rx_buff;
    std::string data{(const char *)rx_buff.data().data(), bytes};
    spdlog::debug("http_actor_t::on_request_read ({}): \n{}", registry_name, data);
    */

    if (keep_alive && http_response.keep_alive()) {
        kept_alive = true;
    } else {
        kept_alive = false;
        transport.reset();
    }

    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }
    process();
}

void http_actor_t::on_io_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    kept_alive = false;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("{}, on_io_error :: {}", identity, ec.message());
    }
    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }
    if (!need_response || stop_io) {
        return process();
    }

    reply_with_error(*queue.front(), make_error(ec));
    queue.pop_front();
    need_response = false;
}

void http_actor_t::on_handshake(bool, X509 *, const tcp::endpoint &, const model::device_id_t *) noexcept {
    resources->release(resource::io);
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }
    write_request();
}

void http_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("{}, on_handshake_error :: {}", identity, ec.message());
    }
    if (!need_response || stop_io) {
        return process();
    }
    reply_with_error(*queue.front(), make_error(ec));
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
        reply_to(*queue.front(), std::move(http_response), response_size, std::move(local_address));
    } else {
        auto ec = r::make_error_code(r::error_code_t::request_timeout);
        reply_with_error(*queue.front(), make_error(ec));
    }
    queue.pop_front();
    need_response = false;

    if (!kept_alive) {
        cancel_io();
    }
    process();
}

void http_actor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    r::actor_base_t::on_start();
}

void http_actor_t::shutdown_finish() noexcept {
    spdlog::trace("{}, shutdown_finish", identity);
    r::actor_base_t::shutdown_finish();
    transport.reset();
}

void http_actor_t::on_close_connection(message::http_close_connection_t &) noexcept {
    if (kept_alive) {
        stop_io = true;
        if (queue.empty() && resources->has(resource::io)) {
            transport->cancel();
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
