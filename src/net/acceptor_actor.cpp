// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "acceptor_actor.h"
#include "names.h"
#include "utils/format.hpp"
#include "utils/error_code.h"
#include "utils/network_interface.h"
#include "utils/format.hpp"
#include "model/messages.h"
#include "model/diff/contact/connect_request.h"
#include "model/diff/contact/update_contact.h"

using namespace syncspirit::net;
using namespace syncspirit::model::diff;

namespace {
namespace resource {
r::plugin::resource_id_t accepting = 0;
}
} // namespace

acceptor_actor_t::acceptor_actor_t(config_t &config)
    : r::actor_base_t{config}, strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()},
      acceptor(strand.context()), peer(strand.context()), cluster{config.cluster} {}

void acceptor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.acceptor", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator, true).link(false); });
}

void acceptor_actor_t::on_start() noexcept {

    LOG_TRACE(log, "on_start");
    sys::error_code ec;

    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        LOG_ERROR(log, "cannot open endpoint ({}) : {}", endpoint, ec.message());
        return do_shutdown(make_error(ec));
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        LOG_ERROR(log, "cannot bind endpoint ({}) : {}", endpoint, ec.message());
        return do_shutdown(make_error(ec));
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        LOG_ERROR(log, "cannot listen ({}) : {}", endpoint, ec.message());
        return do_shutdown(make_error(ec));
    }

    endpoint = acceptor.local_endpoint(ec);
    if (ec) {
        LOG_ERROR(log, "cannot get local endpoint {}", ec.message());
        return do_shutdown(make_error(ec));
    }

    auto uris = utils::local_interfaces(endpoint, log);
    if (log->level() <= spdlog::level::debug) {
        for (auto &uri : uris) {
            log->debug("accepting on {}", uri);
        }
    }

    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new contact::update_contact_t(*cluster, cluster->get_device()->device_id(), uris);
    send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    accept_next();
    r::actor_base_t::on_start();
}

void acceptor_actor_t::accept_next() noexcept {
    resources->acquire(resource::accepting);
    auto fwd = ra::forwarder_t(*this, &acceptor_actor_t::on_accept);
    acceptor.async_accept(peer, std::move(fwd));
}

void acceptor_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    if (resources->has(resource::accepting)) {
        sys::error_code ec;
        acceptor.cancel(ec);
        if (ec) {
            LOG_ERROR(log, "cannot cancel accepting :: ", ec.message());
        }
    }
}

void acceptor_actor_t::on_accept(const sys::error_code &ec) noexcept {
    resources->release(resource::accepting);
    if (ec) {
        if (ec != asio::error::operation_aborted) {
            LOG_WARN(log, "accepting error :: ", ec.message());
            return do_shutdown(make_error(ec));
        } else {
            shutdown_continue();
        }
        return;
    }
    sys::error_code err;
    auto remote = peer.remote_endpoint(err);
    if (err) {
        LOG_WARN(log, "on_accept, cannot get remote endpoint:: {}", err.message());
        return accept_next();
    }
    LOG_TRACE(log, "on_accept, peer = {}", remote);

    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new contact::connect_request_t(std::move(peer), remote);
    send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    accept_next();
}
