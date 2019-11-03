#include "acceptor_actor.h"
#include "names.h"
#include "../utils/error_code.h"

using namespace syncspirit::net;

acceptor_actor_t::acceptor_actor_t(ra::supervisor_asio_t &sup, r::address_ptr_t registry_)
    : r::actor_base_t::actor_base_t(sup), strand{static_cast<ra::supervisor_asio_t &>(supervisor).get_strand()},
      io_context{strand.context()}, acceptor{io_context}, peer(io_context), registry_addr{registry_} {
    accepting = false;
}

void acceptor_actor_t::on_registration(r::message::registration_response_t &msg) noexcept {
    auto &ec = msg.payload.ec;
    if (ec) {
        spdlog::warn("acceptor_actor_t::on_registration failure :: {}", ec.message());
        return;
    }

    unsubscribe(&acceptor_actor_t::on_registration);
    r::actor_base_t::init_start();
}

void acceptor_actor_t::init_start() noexcept {
    subscribe(&acceptor_actor_t::on_listen_request);
    subscribe(&acceptor_actor_t::on_registration);
    request<r::payload::registration_request_t>(registry_addr, names::acceptor, address).send(default_timeout);
}

void acceptor_actor_t::on_shutdown(r::message::shutdown_request_t &msg) noexcept {
    spdlog::trace("acceptor_actor_t::on_shutdown");
    if (accepting) {
        sys::error_code ec;
        acceptor.cancel(ec);
        if (ec) {
            spdlog::error("acceptor_actor_t:: cannot cancel acceptor : ", ec.message());
        }
    }
    redirect_to.reset();
    send<r::payload::deregistration_notify_t>(registry_addr, address);
    registry_addr.reset();
    r::actor_base_t::on_shutdown(msg);
}

void acceptor_actor_t::on_listen_request(message::listen_request_t &msg) noexcept {
    spdlog::trace("acceptor_actor_t::on_listen_request");
    auto &payload = msg.payload.request_payload;
    auto &addr = payload.address;
    auto &port = payload.port;

    tcp::endpoint endpoint(addr, port);
    sys::error_code ec;

    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("cannot open endpoint ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(),
                      ec.message());
        reply_with_error(msg, ec);
        return do_shutdown();
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("cannot bind endpoint ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(),
                      ec.message());
        reply_with_error(msg, ec);
        return do_shutdown();
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("cannot listen ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(), ec.message());
        reply_with_error(msg, ec);
        return do_shutdown();
    }

    accept_next();
    reply_to(msg, acceptor.local_endpoint());
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
        /*
        send<new_peer_t>(redirect_to, std::move(peer));
        peer = tcp_socket_t(io_context);
        accept_next();
        */
        assert(0 && "TODO");
    }
}
