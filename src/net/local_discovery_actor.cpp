#include "local_discovery_actor.h"
#include "names.h"
#include "proto/bep_support.h"
#include "utils/error_code.h"
#include "model/diff/modify/update_contact.h"
#include "model/messages.h"

using namespace syncspirit::net;

static const constexpr std::size_t BUFF_SZ = 1500;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t timer = 1;
} // namespace resource
} // namespace

local_discovery_actor_t::local_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, frequency{r::pt::seconds(cfg.frequency)},
      strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()}, sock{strand.context()},
      port{cfg.port}, cluster{cfg.cluster} {
    log = utils::get_logger("net.local_discovery");
    rx_buff.resize(BUFF_SZ);
    tx_buff.resize(BUFF_SZ);
}

void local_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("local_discovery", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator, true).link(false); });
}

void local_discovery_actor_t::init() noexcept {
    LOG_TRACE(log, "{}, init, will announce to port = {}", identity, port);

    sys::error_code ec;

    auto bc_endpoint = udp::endpoint(asio::ip::address_v4::broadcast(), port);
    sock.open(bc_endpoint.protocol(), ec);
    if (ec) {
        LOG_WARN(log, "{}, init, can't open socket :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    auto listen_endpoint = udp::endpoint{asio::ip::address_v4::loopback(), port};
    sock.bind(listen_endpoint, ec);
    if (ec) {
        LOG_WARN(log, "{}, init, can't bind socket :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    sock.set_option(udp_socket_t::broadcast(true), ec);
    if (ec) {
        LOG_WARN(log, "{}, init, can't set broadcast option :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }
}

void local_discovery_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
    init();
    do_read();
    announce();
}

void local_discovery_actor_t::shutdown_start() noexcept {
    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }
    if (resources->has(resource::io)) {
        sys::error_code ec;
        sock.cancel(ec);
        if (ec) {
            LOG_WARN(log, "{}, shutdown_start, socket cancellation error:: {}", identity, ec.message());
        }
    }
    r::actor_base_t::shutdown_start();
}

void local_discovery_actor_t::announce() noexcept {
    static const constexpr std::uint64_t instance = 0;
    // LOG_TRACE(log, "local_discovery_actor_t::announce", (void *)address.get());

    auto device = cluster->get_device();
    auto &uris = device->get_uris();
    if (!uris.empty()) {
        auto digest = device->device_id().get_sha256();
        auto sz = proto::make_announce_message(tx_buff, digest, uris, instance);
        auto buff = asio::buffer(tx_buff.data(), sz);
        auto fwd_send =
            ra::forwarder_t(*this, &local_discovery_actor_t::on_write, &local_discovery_actor_t::on_write_error);
        auto bc_endpoint = udp::endpoint(asio::ip::address_v4::broadcast(), port);
        sock.async_send_to(buff, bc_endpoint, std::move(fwd_send));
        resources->acquire(resource::io);
        LOG_TRACE(log, "{}, announce has been sent", identity);
    } else {
        LOG_TRACE(log, "{}, announce() skipping", identity);
    }

    timer_request = start_timer(frequency, *this, &local_discovery_actor_t::on_timer);
    resources->acquire(resource::timer);
}

void local_discovery_actor_t::do_read() noexcept {
    auto buff = asio::buffer(rx_buff.data(), BUFF_SZ);
    auto fwd_receive =
        ra::forwarder_t(*this, &local_discovery_actor_t::on_read, &local_discovery_actor_t::on_read_error);
    sock.async_receive_from(buff, peer_endpoint, std::move(fwd_receive));
    resources->acquire(resource::io);
}

void local_discovery_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    timer_request.reset();

    if (!cancelled) {
        announce();
    }
}

void local_discovery_actor_t::on_read(size_t bytes) noexcept {
    resources->release(resource::io);
    // LOG_TRACE(log, "local_discovery_actor_t::on_read");
    auto buff = asio::buffer(rx_buff.data(), bytes);
    auto result = proto::parse_announce(buff);
    if (!result) {
        LOG_TRACE(log, "{}::on_read, cannot parse incoming UDP packet {} bytes from {} :: {}", identity, bytes,
                  peer_endpoint, result.error().message());
    } else {
        auto &msg = result.value();
        auto &sha = msg->id();
        auto device_id = model::device_id_t::from_sha256(sha);
        if (device_id) {
            if (device_id != cluster->get_device()->device_id()) { // skip "self" discovery via network
                utils::uri_container_t uris;
                for (int i = 0; i < msg->addresses_size(); ++i) {
                    auto uri = utils::parse(msg->addresses(i).c_str());
                    if (uri && uri->port) {
                        uris.emplace_back(std::move(uri.value()));
                    }
                }
                if (!uris.empty()) {
                    LOG_TRACE(log, "{}, on_read, local peer = {} ", identity, device_id.value().get_value());
                    for (auto &uri : uris) {
                        LOG_TRACE(log, "{}, on_read, peer is available via {}", identity, uri.full);
                    }
                    pt::ptime now(pt::microsec_clock::local_time());

                    using namespace model::diff;
                    auto diff = model::diff::contact_diff_ptr_t{};
                    diff = new modify::update_contact_t(*cluster, device_id.value(), uris);
                    send<model::payload::contact_update_t>(coordinator, std::move(diff), this);
                } else {
                    LOG_WARN(log, "{}, on_read, no valid uris from: {}", identity, peer_endpoint);
                }
            }
        } else {
            LOG_WARN(log, "{}, on_read, wrong device id, coming from: {}", identity, peer_endpoint);
        }
    }
    return do_read();
}

void local_discovery_actor_t::on_read_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        LOG_ERROR(log, "{}, on_read_error, error = {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
}

void local_discovery_actor_t::on_write(size_t) noexcept { resources->release(resource::io); }

void local_discovery_actor_t::on_write_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        LOG_ERROR(log, "{}, on_write_error, error = {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
}
