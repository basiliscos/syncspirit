#include "acceptor_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "spdlog/spdlog.h"

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t accepting = 0;
}
} // namespace

acceptor_actor_t::acceptor_actor_t(config_t &config)
    : r::actor_base_t{config}, strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()},
      sock(strand.context()), acceptor(strand.context()), peer(strand.context()) {}

void acceptor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&acceptor_actor_t::on_endpoint_request); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::acceptor, get_address());
        p.discover_name(names::coordinator, coordinator, true).link(false);
    });
}

void acceptor_actor_t::on_start() noexcept {
    spdlog::trace("acceptor_actor_t::on_start (addr = {})", (void *)address.get());
    sys::error_code ec;

    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("cannot open endpoint ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(),
                      ec.message());
        return do_shutdown();
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("cannot bind endpoint ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(),
                      ec.message());
        return do_shutdown();
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("cannot listen ({0}:{1}) : {2}", endpoint.address().to_string(), endpoint.port(), ec.message());
        return do_shutdown();
    }

    endpoint = acceptor.local_endpoint(ec);
    if (ec) {
        spdlog::error("cannot get local endpoint {}", ec.message());
        return do_shutdown();
    }

    accept_next();
    r::actor_base_t::on_start();
}

void acceptor_actor_t::accept_next() noexcept {
    resources->acquire(resource::accepting);
    auto fwd = ra::forwarder_t(*this, &acceptor_actor_t::on_accept);
    acceptor.async_accept(peer, std::move(fwd));
}

void acceptor_actor_t::on_endpoint_request(message::endpoint_request_t &request) noexcept {
    reply_to(request, endpoint);
}

void acceptor_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    if (resources->has(resource::accepting)) {
        sys::error_code ec;
        acceptor.cancel(ec);
        if (ec) {
            spdlog::error("cannot cancel accepting :: ", ec.message());
        }
    }
}

void acceptor_actor_t::on_accept(const sys::error_code &ec) noexcept {
    resources->release(resource::accepting);
    if (ec) {
        if (ec != asio::error::operation_aborted) {
            spdlog::warn("accepting error :: ", ec.message());
            do_shutdown();
        } else {
            shutdown_continue();
        }
        return;
    }
    sys::error_code err;
    auto remote = peer.remote_endpoint(err);
    if (err) {
        spdlog::trace("acceptor_actor_t::on_accept, cannot get remote endpoint:: {}", err.message());
        return accept_next();
    }
    spdlog::trace("acceptor_actor_t::on_accept, peer = {}, sock = {}", remote, peer.native_handle());
    send<payload::connection_notify_t>(coordinator, std::move(peer), remote);
    accept_next();
}
