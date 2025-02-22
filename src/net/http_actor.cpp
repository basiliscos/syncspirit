// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "http_actor.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "names.h"

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t request_timer = 1;
r::plugin::resource_id_t resolver = 2;
} // namespace resource
} // namespace

http_actor_t::http_actor_t(config_t &config)
    : r::actor_base_t{config}, resolve_timeout(config.resolve_timeout), request_timeout(config.request_timeout),
      registry_name{config.registry_name}, keep_alive{config.keep_alive} {}

void http_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(registry_name, false);
        log = utils::get_logger(identity);
    });
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
    LOG_TRACE(log, "on request, url = {}", req.payload.request_payload->url);
    queue.emplace_back(&req);
    process();
}

void http_actor_t::on_cancel(message::http_cancel_t &req) noexcept {
    LOG_TRACE(log, "on_cancel, queue is empty = {}", queue.empty());
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
        bool reuse = false;
        if (url->host() == resolved_url->host() && url->port() == resolved_url->port()) {
            if (!last_read) {
                reuse = true;
            } else {
                auto now = clock_t::local_time();
                auto deadline = *last_read + request_timeout;
                reuse = deadline > now;
            }
        } else {
            LOG_DEBUG(log, "different endpoint is used: {} vs {}", resolved_url, url);
        }
        if (reuse) {
            LOG_DEBUG(log, "reusing connection");
            spawn_timer();
            write_request();
        } else {
            LOG_TRACE(log, "will use new connection");
            kept_alive = false;
            transport->cancel();
            return process();
        }

    } else {
        auto host = url->host();
        auto port = url->port_number();
        resolve_request = request<payload::address_request_t>(resolver, host, port).send(resolve_timeout);
        resources->acquire(resource::resolver);
    }
}

void http_actor_t::spawn_timer() noexcept {
    assert(!timer_request);
    timer_request = start_timer(request_timeout, *this, &http_actor_t::on_timer);
    resources->acquire(resource::request_timer);
}

void http_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    resources->release(resource::resolver);
    resolve_request.reset();
    auto &ee = res.payload.ee;

    if (ee) {
        LOG_WARN(log, "on_resolve error: {}", ee->message());
        reply_with_error(*queue.front(), ee);
        queue.pop_front();
        need_response = false;
        return process();
    }

    if (stop_io || queue.empty())
        return process();

    if (state != r::state_t::OPERATIONAL) {
        return;
    }

    auto &payload = queue.front()->payload.request_payload;
    auto &ssl_ctx = payload->ssl_context;
    auto sup = static_cast<ra::supervisor_asio_t *>(supervisor);
    transport::transport_config_t cfg{std::move(ssl_ctx), payload->url, *sup, {}, true};
    transport = transport::initiate_http(cfg);
    if (!transport) {
        auto ec = utils::make_error_code(utils::error_code_t::transport_not_available);
        reply_with_error(*queue.front(), make_error(ec));
        queue.pop_front();
        need_response = false;
        return process();
    }

    auto &ips = res.payload.res->results;
    auto port = res.payload.req->payload.request_payload->port;
    auto endpoints = utils::make_endpoints<tcp, tcp::endpoint>(ips, port);

    transport::connect_fn_t on_connect = [&](const auto &arg) { this->on_connect(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg); };
    transport->async_connect(std::move(endpoints), on_connect, on_error);
    resources->acquire(resource::io);
    spawn_timer();
    resolved_url = payload->url;
}

void http_actor_t::on_connect(const tcp::endpoint &) noexcept {
    LOG_TRACE(log, "on_connect");
    resources->release(resource::io);
    if (!need_response || stop_io) {
        return process();
    }

    if (queue.front()->payload.request_payload->local_ip) {
        sys::error_code ec;
        local_address = transport->local_address(ec);
        if (ec) {
            LOG_WARN(log, "on_connect, get local addr error :: {}", ec.message());
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
    LOG_TRACE(log, "sending {} bytes to {} ", data.size(), url);
    auto buff = asio::buffer(data.data(), data.size());
    if (payload.debug) {
        std::string_view write_data{(const char *)buff.data(), data.size()};
        LOG_DEBUG(log, "request ({}):\n{}", data.size(), write_data);
    }
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

    auto &req = *queue.front();
    if (req.payload.request_payload->debug) {
        auto &body = http_response.body();
        LOG_DEBUG(log, "response ({}):\n{}", bytes, body);
    }
    if (keep_alive && http_response.keep_alive()) {
        kept_alive = true;
        last_read = clock_t::local_time();
    } else {
        kept_alive = false;
        transport.reset();
    }

    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }

    reply_to(req, std::move(http_response), response_size, std::move(local_address));
    queue.pop_front();
    need_response = false;

    process();
}

void http_actor_t::on_io_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    kept_alive = false;
    if (ec != asio::error::operation_aborted) {
        LOG_DEBUG(log, "on_io_error :: {}", ec.message());
    }
    cancel_io();
    if (!need_response || stop_io) {
        return process();
    }

    reply_with_error(*queue.front(), make_error(ec));
    queue.pop_front();
    need_response = false;
}

void http_actor_t::on_handshake(bool, utils::x509_t &, const tcp::endpoint &, const model::device_id_t *) noexcept {
    resources->release(resource::io);
    if (!need_response || stop_io) {
        return process();
    }
    if (state == r::state_t::OPERATIONAL) {
        write_request();
    }
}

void http_actor_t::on_handshake_error(sys::error_code ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "on_handshake_error :: {}", ec.message());
    }
    if (!need_response || stop_io) {
        return process();
    }
    reply_with_error(*queue.front(), make_error(ec));
    queue.pop_front();
    need_response = false;
    cancel_io();
    process();
}

void http_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::request_timer);
    timer_request.reset();

    bool do_cancel_io = false;
    if (!cancelled) {
        auto ec = r::make_error_code(r::error_code_t::request_timeout);
        reply_with_error(*queue.front(), make_error(ec));
        queue.pop_front();
        need_response = false;
        if (!kept_alive) {
            do_cancel_io = true;
        }
    } else {
        do_cancel_io = true;
    }

    if (do_cancel_io) {
        cancel_io();
    }
}

void http_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void http_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    r::actor_base_t::shutdown_finish();
    transport.reset();
}

void http_actor_t::on_close_connection(message::http_close_connection_t &) noexcept {
    LOG_TRACE(log, "on_close_connection");
    stop_io = true;
    cancel_io();
}

void http_actor_t::cancel_io() noexcept {
    if (resources->has(resource::io)) {
        transport->cancel();
    }
    if (resources->has(resource::resolver)) {
        send<message::resolve_cancel_t::payload_t>(resolver, *resolve_request, get_address());
    }
    if (resources->has(resource::request_timer)) {
        cancel_timer(*timer_request);
    }
}
