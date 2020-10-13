#include "http_actor.h"
#include "../utils/error_code.h"
#include "spdlog/spdlog.h"
#include "names.h"

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t request_timer = 1;
r::plugin::resource_id_t shutdown_timer = 2;
r::plugin::resource_id_t connection = 3;
} // namespace resource
} // namespace

http_actor_t::http_actor_t(config_t &config)
    : r::actor_base_t{config}, resolve_timeout(config.resolve_timeout),
      request_timeout(config.request_timeout), registry_name{config.registry_name},
      keep_alive{config.keep_alive}, strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()},
      request_timer{strand.context()}, shutdown_timer{strand.context()} {}

void http_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&http_actor_t::on_request);
        p.subscribe_actor(&http_actor_t::on_resolve);
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

void http_actor_t::process() noexcept {
    if (stop_io) {
        auto ec = utils::make_error_code(utils::error_code::service_not_available);
        for (auto req : queue) {
            reply_with_error(*req, ec);
        }
        queue.clear();
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
        request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
    }
}

void http_actor_t::spawn_timer() noexcept {
    resources->acquire(resource::io);

    request_timer.expires_from_now(request_timeout);
    auto fwd_timer = ra::forwarder_t(*this, &http_actor_t::on_timer_trigger, &http_actor_t::on_timer_error);
    request_timer.async_wait(std::move(fwd_timer));
    resources->acquire(resource::request_timer);
}

void http_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
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

    tcp::socket *layer;
    auto &payload = queue.front()->payload.request_payload;
    auto &ssl_ctx = payload->ssl_context;
    if (ssl_ctx) {
        sock_s = std::make_unique<secure_socket_t::element_type>(strand, ssl_ctx->ctx);
        auto &host = payload->url.host;
        if (!SSL_set_tlsext_host_name(sock_s->native_handle(), host.c_str())) {
            sys::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            spdlog::error("http_actor_t:: Set SNI Hostname : {}", ec.message());
            reply_with_error(*queue.front(), ec);
            queue.pop_front();
            need_response = false;
            return process();
        }
        sock_s->set_verify_depth(ssl_ctx->verify_depth);
        sock_s->set_verify_mode(ssl_ctx->verify_mode);
        sock_s->set_verify_callback(ssl_ctx->verify_callback);
        layer = &sock_s->next_layer();
    } else {
        sock = std::make_unique<tcp::socket>(strand.context());
        layer = sock.get();
    }

    auto &addresses = res.payload.res->results;
    auto fwd_connect = ra::forwarder_t(*this, &http_actor_t::on_connect, &http_actor_t::on_tcp_error);
    asio::async_connect(*layer, addresses.begin(), addresses.end(), std::move(fwd_connect));

    spawn_timer();
    resolved_url = payload->url;
}

void http_actor_t::on_connect(resolve_it_t) noexcept {
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }

    if (sock) {
        write_request();
    } else {
        auto fwd = ra::forwarder_t(*this, &http_actor_t::on_handshake, &http_actor_t::on_handshake_error);
        sock_s->async_handshake(ssl::stream_base::client, std::move(fwd));
    }
}

void http_actor_t::write_request() noexcept {
    auto &payload = *queue.front()->payload.request_payload;
    auto &url = payload.url;
    auto &data = payload.data;
    spdlog::trace("http_actor_t ({}) :: sending {} bytes to {} ", registry_name, data.size(), url.full);
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_request_sent, &http_actor_t::on_tcp_error);
    auto buff = asio::buffer(data.data(), data.size());
    if (sock) {
        asio::async_write(*sock, buff, std::move(fwd));
    } else {
        asio::async_write(*sock_s, buff, std::move(fwd));
    }
}

void http_actor_t::on_request_sent(std::size_t /* bytes */) noexcept {
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }

    auto &payload = *queue.front()->payload.request_payload;
    auto &rx_buff = payload.rx_buff;
    rx_buff->prepare(payload.rx_buff_size);
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_request_read, &http_actor_t::on_tcp_error);
    if (sock) {
        http::async_read(*sock, *rx_buff, http_response, std::move(fwd));
    } else {
        http::async_read(*sock_s, *rx_buff, http_response, std::move(fwd));
    }
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
        sys::error_code ec;
        if (sock) {
            sock->close(ec);
        } else {
            sock_s->next_layer().close(ec);
        }
        if (ec) {
            // we are going to destroy socket anyway
            spdlog::trace("http_actor_t:: closing socket error: {} ", ec.message());
        }
        sock.reset();
        sock_s.reset();
    }

    resources->release(resource::io);
    cancel_timer();
    process();
}

void http_actor_t::on_tcp_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (resources->has(resource::connection)) {
        resources->release(resource::connection);
    }
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_tcp_error :: {}", ec.message());
    }
    cancel_timer();
    if (!need_response || stop_io) {
        return process();
    }

    reply_with_error(*queue.front(), ec);
    queue.pop_front();
    need_response = false;
}

void http_actor_t::on_timer_error(const sys::error_code &ec) noexcept {
    resources->release(resource::request_timer);
    if (ec != asio::error::operation_aborted) {
        if (need_response) {
            reply_with_error(*queue.front(), ec);
            queue.pop_front();
            need_response = false;
        }
        spdlog::error("http_actor_t::on_timer_error() :: {}", ec.message());
        return get_supervisor().do_shutdown();
    }

    if (need_response) {
        reply_to(*queue.front(), std::move(http_response), response_size);
        queue.pop_front();
        need_response = false;
    }
    if (!resources->has(resource::connection)) {
        cancel_sock();
    }
    process();
}

void http_actor_t::on_handshake() noexcept {
    if (!need_response || stop_io) {
        resources->release(resource::io);
        return process();
    }
    write_request();
}

void http_actor_t::on_handshake_error(const sys::error_code &ec) noexcept {
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
    cancel_timer();
    process();
}

void http_actor_t::on_timer_trigger() noexcept {
    resources->release(resource::request_timer);
    if (!need_response || stop_io) {
        return process();
    }

    auto ec = r::make_error_code(r::error_code_t::request_timeout);
    reply_with_error(*queue.front(), ec);
    queue.pop_front();
    need_response = false;
    cancel_sock();
    process();
}

void http_actor_t::cancel_sock() noexcept {
    sys::error_code ec;
    if (resources->has(resource::connection)) {
        resources->release(resource::connection);
    }
    if (sock) {
        sock->cancel(ec);
    } else if (sock_s) {
        sock_s->next_layer().cancel(ec);
    }
    if (ec) {
        spdlog::error("http_actor_t::cancel_sock() :: {}", ec.message());
        get_supervisor().do_shutdown();
    }
}

void http_actor_t::cancel_timer() noexcept {
    sys::error_code ec;
    request_timer.cancel(ec);
    if (ec) {
        spdlog::error("http_actor_t::cancel_timer() :: {}", ec.message());
        get_supervisor().do_shutdown();
    }
}

void http_actor_t::on_start() noexcept {
    spdlog::trace("http_actor_t::on_start({}) (addr = {})", registry_name, (void *)address.get());
    r::actor_base_t::on_start();
}

void http_actor_t::shutdown_start() noexcept {
    if (resources->has(resource::io) || resources->has(resource::request_timer)) {
        start_shutdown_timer();
    } else if (resources->has(resource::connection)) {
        resources->release(resource::connection);
    }
    r::actor_base_t::shutdown_start();
}

void http_actor_t::start_shutdown_timer() noexcept {
    shutdown_timer.expires_from_now(request_timeout);
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_shutdown_timer_trigger, &http_actor_t::on_shutdown_timer_error);
    shutdown_timer.async_wait(std::move(fwd));
    resources->acquire(resource::shutdown_timer);
}

void http_actor_t::on_shutdown_timer_error(const sys::error_code &ec) noexcept {
    resources->release(resource::shutdown_timer);
    if (ec != asio::error::operation_aborted) {
        spdlog::error("http_actor_t::on_timer_error() :: {}", ec.message());
    }

    cancel_io();
    stop_io = true;
    process();
    shutdown_continue();
}

void http_actor_t::on_shutdown_timer_trigger() noexcept {
    resources->release(resource::shutdown_timer);
    spdlog::warn("http_actor_t::on_shutdown_timer_trigger", (void *)address.get());

    cancel_io();
    stop_io = true;
    process();
    shutdown_continue();
}

void http_actor_t::cancel_io() noexcept {
    if (resources->has(resource::io)) {
        if (sock) {
            sock->cancel();
        } else {
            sock_s->next_layer().cancel();
        }
    }
    if (resources->has(resource::request_timer)) {
        request_timer.cancel();
    }
}
