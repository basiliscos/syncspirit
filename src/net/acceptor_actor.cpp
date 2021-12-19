#include "acceptor_actor.h"
#include "names.h"
#include "utils/error_code.h"
#include "model/diff/contact_diff.h"
#include "model/diff/modify/connect_request.h"
#include "model/diff/modify/update_contact.h"

using namespace syncspirit::net;
using namespace syncspirit::model::diff;

namespace {
namespace resource {
r::plugin::resource_id_t accepting = 0;
}
} // namespace

acceptor_actor_t::acceptor_actor_t(config_t &config)
    : r::actor_base_t{config}, strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()},
      sock(strand.context()), acceptor(strand.context()), peer(strand.context()), cluster{config.cluster} {
    log = utils::get_logger("net.acceptor");
}

void acceptor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(log->name(), false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::coordinator, coordinator, true).link(false);
    });
}

void acceptor_actor_t::on_start() noexcept {

    LOG_TRACE(log, "{}, on_start", identity);
    sys::error_code ec;

    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        LOG_ERROR(log, "{}, cannot open endpoint ({0}:{1}) : {2}", identity, endpoint.address().to_string(),
                  endpoint.port(), ec.message());
        return do_shutdown(make_error(ec));
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        LOG_ERROR(log, "{}, cannot bind endpoint ({0}:{1}) : {2}", identity, endpoint.address().to_string(),
                  endpoint.port(), ec.message());
        return do_shutdown(make_error(ec));
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        LOG_ERROR(log, "{}, cannot listen ({0}:{1}) : {2}", identity, endpoint.address().to_string(), endpoint.port(),
                  ec.message());
        return do_shutdown(make_error(ec));
    }

    endpoint = acceptor.local_endpoint(ec);
    if (ec) {
        LOG_ERROR(log, "{}, cannot get local endpoint {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    auto uri_str = fmt::format("tcp://{0}:{1}/", endpoint.address().to_string(), endpoint.port());
    auto uri = utils::parse(uri_str).value();
    auto uris = utils::uri_container_t{uri};
    LOG_TRACE(log, "{}, accepting on {}", identity, uri.full);

    auto diff = model::diff::contact_diff_ptr_t{};
    diff = new modify::update_contact_t(*cluster, cluster->get_device()->device_id(), uris);
    send<payload::contact_update_t>(coordinator, std::move(diff), this);
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
            LOG_ERROR(log, "{}, cannot cancel accepting :: ", identity, ec.message());
        }
    }
}

void acceptor_actor_t::on_accept(const sys::error_code &ec) noexcept {
    resources->release(resource::accepting);
    if (ec) {
        if (ec != asio::error::operation_aborted) {
            LOG_WARN(log, "{}, accepting error :: ", identity, ec.message());
            return do_shutdown(make_error(ec));
        } else {
            shutdown_continue();
        }
        return;
    }
    sys::error_code err;
    auto remote = peer.remote_endpoint(err);
    if (err) {
        LOG_WARN(log, "{}, on_accept, cannot get remote endpoint:: {}", identity, err.message());
        return accept_next();
    }
    LOG_TRACE(log, "{}, on_accept, peer = {}, sock = {}", identity, remote, peer.native_handle());

    auto diff = model::diff::contact_diff_ptr_t{};
    diff = new modify::connect_request_t(std::move(peer), remote);
    send<payload::contact_update_t>(coordinator, std::move(diff), this);
    accept_next();
}
