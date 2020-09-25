#include "http_actor.h"
#include "../utils/error_code.h"
#include "spdlog/spdlog.h"
#include "names.h"

using namespace syncspirit::net;

namespace {
namespace resource {
    r::plugin::resource_id_t io = 0;
    r::plugin::resource_id_t timer = 1;
}
}

http_actor_t::http_actor_t(config_t &config): r::actor_base_t{config}, resolve_timeout(config.resolve_timeout), request_timeout(config.request_timeout),
    strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()}, timer{strand.context()} {}

void http_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&http_actor_t::on_request);
        p.subscribe_actor(&http_actor_t::on_resolve);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::http10, get_address());
        p.discover_name(names::resolver, resolver).link(false);
    });
}

bool http_actor_t::maybe_shutdown() noexcept {
    if (state == r::state_t::SHUTTING_DOWN) {
        if (resources->has(resource::io)) {
            sock->cancel();
        }
        if (resources->has(resource::timer)) {
            timer.cancel();
        }
        return true;
    }
    return false;
}

void http_actor_t::on_request(message::http_request_t &req) noexcept {
    assert(!sock);
    orig_req.reset(&req);
    http_response.clear();
    need_response = true;
    response_size = 0;
    auto &url = req.payload.request_payload->url;
    auto port = std::to_string(url.port);
    request<payload::address_request_t>(resolver, url.host, port).send(resolve_timeout);
}

void http_actor_t::on_resolve(message::resolve_response_t &res) noexcept {
    auto &ec = res.payload.ec;
    if (ec) {
        reply_with_error(*orig_req, ec);
        need_response = false;
        return;
    }

    sys::error_code ec_sock;
    sock = std::make_unique<tcp::socket>(strand.context());
    sock->open(tcp::v4(), ec_sock);
    if (ec_sock) {
        reply_with_error(*orig_req, ec_sock);
        orig_req.reset();
        need_response = false;
        return;
    }

    auto &addresses = res.payload.res->results;
    auto fwd_connect = ra::forwarder_t(*this, &http_actor_t::on_connect, &http_actor_t::on_tcp_error);
    asio::async_connect(*sock, addresses.begin(), addresses.end(), std::move(fwd_connect));
    resources->acquire(resource::io);

    timer.expires_from_now(request_timeout);
    auto fwd_timer = ra::forwarder_t(*this, &http_actor_t::on_timer_trigger, &http_actor_t::on_timer_error);
    timer.async_wait(std::move(fwd_timer));
    resources->acquire(resource::timer);
}

void http_actor_t::on_connect(resolve_it_t) noexcept {
    if (!need_response) {
        resources->release(resource::io);
        return;
    }
    if (maybe_shutdown()) {
        return;
    }

    auto& payload = *orig_req->payload.request_payload;
    auto &url =  payload.url;
    auto &data = payload.data;
    spdlog::trace("http_actor_t:: sending {0} bytes to {1} ", data.size(), url.full);
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_request_sent, &http_actor_t::on_tcp_error);
    auto buff = asio::buffer(data.data(), data.size());
    asio::async_write(*sock, buff, std::move(fwd));
}

void http_actor_t::on_request_sent(std::size_t /* bytes */) noexcept {
    if (!orig_req) {
        resources->release(resource::io);
        return;
    }
    if (maybe_shutdown())
        return;

    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_request_read, &http_actor_t::on_tcp_error);

    auto &rx_buff = orig_req->payload.request_payload->rx_buff;
    rx_buff->prepare(orig_req->payload.request_payload->rx_buff_size);
    http::async_read(*sock, *rx_buff, http_response, std::move(fwd));
}

void http_actor_t::on_request_read(std::size_t bytes) noexcept {
    response_size = bytes;

    sys::error_code ec;
    /* sock->cancel(); */
    sock->close(ec);
    if (ec) {
        // we are going to destroy socket anyway
        std::cout << "socket closing error: " << ec.message() << "\n";
    }
    sock.reset();
    resources->release(resource::io);
    cancel_timer();
    maybe_shutdown();
}

void http_actor_t::on_tcp_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (!need_response) {
        return;
    }

    reply_with_error(*orig_req, ec);
    need_response = false;
    cancel_timer();
}

void http_actor_t::on_timer_error(const sys::error_code &ec) noexcept {
    resources->release(resource::timer);
    if (ec != asio::error::operation_aborted) {
        if (need_response) {
            reply_with_error(*orig_req, ec);
            need_response = false;
        }
        spdlog::error("http_actor_t::on_timer_error() :: {}", ec.message());
        return get_supervisor().do_shutdown();
    }

    if (need_response) {
        reply_to(*orig_req, std::move(http_response), response_size);
        need_response = false;
        orig_req.reset();
    }
    cancel_sock();
    maybe_shutdown();
}


void http_actor_t::on_timer_trigger() noexcept {
    resources->release(resource::timer);
    if (need_response) {
        auto ec = r::make_error_code(r::error_code_t::request_timeout);
        reply_with_error(*orig_req, ec);
        need_response = false;
    }
    cancel_sock();
    maybe_shutdown();
}

bool http_actor_t::cancel_sock() noexcept {
    sys::error_code ec;
    if (sock) {
        sock->cancel(ec);
        if (ec) {
            spdlog::error("http_actor_t::cancel_sock() :: {}", ec.message());
            get_supervisor().do_shutdown();
        }
    }
    return (bool)ec;
}

bool http_actor_t::cancel_timer() noexcept {
    sys::error_code ec;
    timer.cancel(ec);
    if (ec) {
        spdlog::error("http_actor_t::cancel_timer() :: {}", ec.message());
        get_supervisor().do_shutdown();
    }
    return (bool)ec;
}

void http_actor_t::on_start() noexcept {
    spdlog::trace("http_actor_t::on_start");
    r::actor_base_t::on_start();
}


// cancel any pending async ops
void http_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    maybe_shutdown();
}
