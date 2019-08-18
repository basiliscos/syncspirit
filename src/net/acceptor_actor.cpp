#include "acceptor_actor.h"
#include "../utils/error_code.h"

using namespace syncspirit::net;

acceptor_actor_t::acceptor_actor_t(ra::supervisor_asio_t &sup)
    : r::actor_base_t::actor_base_t(sup), strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, acceptor{io_context}, peer(io_context) {
    accepting = false;
}

void acceptor_actor_t::on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept {
    r::actor_base_t::on_initialize(msg);
    subscribe(&acceptor_actor_t::on_listen_request);
}

void acceptor_actor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    spdlog::trace("acceptor_actor_t::on_shutdown");
    if (accepting) {
        sys::error_code ec;
        acceptor.cancel(ec);
        if (ec) {
            spdlog::error("acceptor_actor_t:: cannot cancel acceptor : ", ec.message());
        }
    }
    redirect_to.reset();
    r::actor_base_t::on_shutdown(msg);
}

void acceptor_actor_t::on_listen_request(r::message_t<listen_request_t> &msg) noexcept {
    spdlog::trace("acceptor_actor_t::on_listen_request");
    tcp::endpoint endpoint(msg.payload.address, msg.payload.port);
    sys::error_code ec;

    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("cannot open endpoint ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(),
                      ec.message());
        send<listen_failure_t>(msg.payload.reply_to, ec);
        return do_shutdown();
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("cannot bind endpoint ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(),
                      ec.message());
        send<listen_failure_t>(msg.payload.reply_to, ec);
        return do_shutdown();
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("cannot listen ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(), ec.message());
        send<listen_failure_t>(msg.payload.reply_to, ec);
        return do_shutdown();
    }

    redirect_to = msg.payload.redirect_to;
    accept_next();
    send<listen_response_t>(msg.payload.reply_to, acceptor.local_endpoint());
}

void acceptor_actor_t::accept_next() noexcept {
    accepting = true;
    auto fwd = ra::forwarder_t(*this, &acceptor_actor_t::on_accept);
    acceptor.async_accept(peer, std::move(fwd));
}

void acceptor_actor_t::on_accept(const sys::error_code &ec) noexcept {
    spdlog::trace("acceptor_actor_t::on_accept");
    accepting = false;
    if (ec) {
        if (ec != asio::error::operation_aborted) {
            spdlog::warn("acceptor_actor_t::on_accept : {}", ec.message());
            return do_shutdown();
        }
        return;
    }

    if (redirect_to) {
        send<new_peer_t>(redirect_to, std::move(peer));
        peer = tcp_socket_t(io_context);
        accept_next();
    }
}
