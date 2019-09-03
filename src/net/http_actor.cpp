#include "http_actor.h"
#include "../utils/error_code.h"

using namespace syncspirit::net;

http_actor_t::http_actor_t(ra::supervisor_asio_t &sup)
    : r::actor_base_t::actor_base_t(sup), strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, resolver{io_context}, activities_flag{0} {}

void http_actor_t::on_initialize(r::message::init_request_t &msg) noexcept {
    subscribe(&http_actor_t::on_request);
    r::actor_base_t::on_initialize(msg);
}

void http_actor_t::clean_state() noexcept {
    if (activities_flag & TCP_ACTIVE) {
        sys::error_code ec;
        sock->cancel(ec);
        if (ec) {
            spdlog::error("http_actor_t:: tcp socket cancellation : {}", ec.message());
        }
    }

    if (activities_flag & RESOLVER_ACTIVE) {
        resolver.cancel();
    }
    request.reset();
}

void http_actor_t::on_shutdown(r::message::shutdown_request_t &msg) noexcept {
    spdlog::trace("http_actor_t::on_shutdown");
    clean_state();
    r::actor_base_t::on_shutdown(msg);
}

void http_actor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("http_actor_t::on_start");
    r::actor_base_t::on_start(msg);
}

void http_actor_t::on_request(message::http_request_t &msg) noexcept {
    spdlog::trace("http_actor_t::on_request");
    if (request) {
        spdlog::error("request is already processing");
        return do_shutdown();
    }
    request.reset(&msg);

    trigger_request();
}

void http_actor_t::trigger_request() noexcept {
    /* some operations might be pending (i.e. timer cancellation) */
    if (activities_flag) {
        auto fwd_postpone = ra::forwarder_t(*this, &http_actor_t::trigger_request);
        return asio::post(io_context, fwd_postpone);
    }

    /* start resolver */
    auto &url = request->payload.request_payload.url;
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_resolve, &http_actor_t::on_resolve_error);
    resolver.async_resolve(url.host, url.service, std::move(fwd));
    activities_flag |= RESOLVER_ACTIVE;
}

void http_actor_t::reply_error(const sys::error_code &ec) noexcept {
    reply_with_error(*request, ec);
    clean_state();
}

void http_actor_t::on_resolve_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~RESOLVER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_resolve_error :: {}", ec.message());
        reply_error(ec);
    }
}

void http_actor_t::on_resolve(resolve_results_t results) noexcept {
    activities_flag &= ~RESOLVER_ACTIVE;

    sock = std::make_unique<tcp_socket_t>(io_context);
    activities_flag |= TCP_ACTIVE;

    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_connect, &http_actor_t::on_tcp_error);
    asio::async_connect(*sock, results.begin(), results.end(), std::move(fwd));
}

void http_actor_t::on_connect(resolve_it_t) noexcept {
    spdlog::trace("http_actor_t::on_connect");
    sys::error_code ec;
    local_endpoint = sock->local_endpoint(ec);
    if (ec) {
        spdlog::warn("http_actor_t::on_discovery_sent :: cannot get local endpoint: {0}", ec.message());
        return reply_error(ec);
    }
    spdlog::trace("http_actor_t::on_connect, local endpoint = {0}:{1}", local_endpoint.address().to_string(),
                  local_endpoint.port());

    auto &req_payload = request->payload.request_payload;
    auto &data = req_payload.data;
    auto &url = req_payload.url;
    spdlog::trace("http_actor_t:: sending {0} bytes to {1} ", data.size(), url.full);
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_request_sent, &http_actor_t::on_tcp_error);
    auto buff = asio::buffer(data.data(), data.size());
    asio::async_write(*sock, buff, std::move(fwd));
}

void http_actor_t::on_tcp_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~TCP_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_resolve_error :: {}", ec.message());
        reply_error(ec);
    }
}

void http_actor_t::on_request_sent(std::size_t bytes) noexcept {
    spdlog::trace("http_actor_t::on_on_request_sent ({} bytes)", bytes);
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_response_received, &http_actor_t::on_tcp_error);
    auto &req_payload = request->payload.request_payload;
    auto &rx_buff = req_payload.rx_buff;
    rx_buff->prepare(req_payload.rx_buff_size);
    http::async_read(*sock, *rx_buff, response, std::move(fwd));
}

void http_actor_t::on_response_received(std::size_t bytes) noexcept {
    activities_flag &= ~TCP_ACTIVE;
    spdlog::trace("http_actor_t::on_response_received ({0} bytes)", bytes);
    reply_to(*request, local_endpoint, std::move(response), bytes);
    // reply_to(*request, "abc", local_endpoint, std::move(response), bytes);
    clean_state();
}
