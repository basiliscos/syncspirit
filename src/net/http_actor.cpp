#include "http_actor.h"
#include "../utils/error_code.h"

using namespace syncspirit::net;

http_actor_t::http_actor_t(ra::supervisor_asio_t &sup)
    : r::actor_base_t::actor_base_t(sup), strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, resolver{io_context}, timer{io_context}, activities_flag{0} {}

void http_actor_t::on_initialize(r::message::init_request_t &msg) noexcept {
    r::actor_base_t::on_initialize(msg);
    subscribe(&http_actor_t::on_request);
}

void http_actor_t::clean_state() noexcept {
    if (activities_flag & TIMER_ACTIVE) {
        sys::error_code ec;
        timer.cancel(ec);
        if (ec) {
            spdlog::error("http_actor_t:: timer cancellation : {}", ec.message());
        }
    }

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
    response.reset();
    sock.reset();
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

void http_actor_t::on_request(request_message_t &msg) noexcept {
    spdlog::trace("http_actor_t::on_request");
    if (request) {
        spdlog::error("request is already processing");
        return do_shutdown();
    }

    request = request_ptr_t{&msg};

    trigger_request();
}

void http_actor_t::trigger_request() noexcept {
    /* some operations might be pending (i.e. timer cancellation) */
    if (activities_flag) {
        auto fwd_postpone = ra::forwarder_t(*this, &http_actor_t::trigger_request);
        return asio::post(io_context, fwd_postpone);
    }

    /* start timer */
    timer.expires_from_now(request->payload.timeout);
    auto fwd_timeout = ra::forwarder_t(*this, &http_actor_t::on_timeout_trigger, &http_actor_t::on_timeout_error);
    timer.async_wait(std::move(fwd_timeout));
    activities_flag |= TIMER_ACTIVE;

    /* start resolver */
    auto &url = request->payload.url;
    auto fwd = ra::forwarder_t(*this, &http_actor_t::on_resolve, &http_actor_t::on_resolve_error);
    resolver.async_resolve(url.host, url.service, std::move(fwd));
    activities_flag |= RESOLVER_ACTIVE;
}

void http_actor_t::reply_error(const sys::error_code &ec) noexcept {
    auto &url = request->payload.url;
    send<request_failed_t>(request->payload.reply_to, ec, url);
    clean_state();
}

void http_actor_t::on_timeout_trigger() noexcept {
    spdlog::trace("http_actor_t::on_timeout_trigger");
    activities_flag &= ~TIMER_ACTIVE;
    auto ec = sys::errc::make_error_code(sys::errc::timed_out);
    reply_error(ec);
}

void http_actor_t::on_timeout_error(const sys::error_code &ec) noexcept {
    activities_flag &= ~TIMER_ACTIVE;
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("http_actor_t::on_timer_error :: {}", ec.message());
        reply_error(ec);
    }
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
    auto endpoint = sock->local_endpoint(ec);
    if (ec) {
        spdlog::warn("http_actor_t::on_discovery_sent :: cannot get local endpoint: {0}", ec.message());
        return reply_error(ec);
    }
    spdlog::trace("http_actor_t::on_connect, local endpoint = {0}:{1}", endpoint.address().to_string(),
                  endpoint.port());
    auto &req_payload = request->payload;
    response = response_ptr_t(
        new response_message_t{req_payload.reply_to, req_payload.rx_buff, std::move(endpoint), req_payload.url});

    auto &data = request->payload.data;
    auto &url = request->payload.url;
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
    auto &rx_buff = response->payload.rx_buff;
    rx_buff->prepare(request->payload.rx_buff_size);
    http::async_read(*sock, *rx_buff, response->payload.response, std::move(fwd));
}

void http_actor_t::on_response_received(std::size_t bytes) noexcept {
    activities_flag &= ~TCP_ACTIVE;
    spdlog::trace("http_actor_t::on_response_received ({0} bytes)", bytes);
    response->payload.url = request->payload.url;
    response->payload.bytes = bytes;
    supervisor.put(response);
    clean_state();
}
