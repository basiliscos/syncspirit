#include <spdlog/spdlog.h>

#include "local_discovery_actor.h"
#include "names.h"
#include "../proto/bep_support.h"
#include "../utils/error_code.h"

using namespace syncspirit::net;

static const constexpr std::size_t BUFF_SZ = 1500;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t timer = 1;
r::plugin::resource_id_t req_acceptor = 2;
} // namespace resource
} // namespace

local_discovery_actor_t::local_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, frequency{r::pt::seconds(cfg.frequency)}, device{cfg.device},
      strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()}, sock{strand.context()},
      bc_endpoint(udp::v4(), cfg.port) {
    rx_buff.resize(BUFF_SZ);
    tx_buff.resize(BUFF_SZ);
}

void local_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("local_discovery", false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        init();
        p.subscribe_actor(&local_discovery_actor_t::on_endpoint);
        auto timeout = shutdown_timeout / 2;
        endpoint_request = request<payload::endpoint_request_t>(acceptor).send(timeout);
        resources->acquire(resource::req_acceptor);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::acceptor, acceptor, true).link(true);
        p.discover_name(names::coordinator, coordinator, true).link(false);
    });
}

void local_discovery_actor_t::init() noexcept {
    spdlog::trace("{}, init, will announce to port = {}", identity, bc_endpoint.port());

    sys::error_code ec;

    sock.open(bc_endpoint.protocol(), ec);
    if (ec) {
        spdlog::warn("{}, init, can't open socket :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    sock.bind(bc_endpoint, ec);
    if (ec) {
        spdlog::warn("{}, init, can't bind socket :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }

    sock.set_option(udp_socket_t::broadcast(true), ec);
    if (ec) {
        spdlog::warn("{}, init, can't set broadcast option :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }
}

void local_discovery_actor_t::on_endpoint(message::endpoint_response_t &res) noexcept {
    spdlog::trace("{}, on_endpoint", identity);
    endpoint_request.reset();
    resources->release(resource::req_acceptor);

    auto &ec = res.payload.ec;
    if (ec) {
        spdlog::warn("{}, on_endpoint, cannot get acceptor endpoint :: {}", identity, ec->message());
        auto inner = utils::make_error_code(utils::error_code::endpoint_failed);
        return do_shutdown(make_error(inner, ec));
    }
    auto &ep = res.payload.res.local_endpoint;
    std::string my_url = fmt::format("tcp://{}:{}", ep.address().to_string(), ep.port());
    uris.emplace_back(std::move(utils::parse(my_url).value()));
}

void local_discovery_actor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    r::actor_base_t::on_start();
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
            spdlog::warn("{}, shutdown_start, socket cancellation error:: {}", identity, ec.message());
        }
    }
    r::actor_base_t::shutdown_start();
}

void local_discovery_actor_t::announce() noexcept {
    static const constexpr std::uint64_t instance = 0;
    // spdlog::trace("local_discovery_actor_t::announce", (void *)address.get());

    auto &digest = device->device_id.get_sha256();
    auto sz = proto::make_announce_message(tx_buff, digest, uris, instance);
    auto buff = asio::buffer(tx_buff.data(), sz);
    auto fwd_send =
        ra::forwarder_t(*this, &local_discovery_actor_t::on_write, &local_discovery_actor_t::on_write_error);
    sock.async_send_to(buff, bc_endpoint, std::move(fwd_send));
    resources->acquire(resource::io);

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
    // spdlog::trace("local_discovery_actor_t::on_read");
    auto buff = asio::buffer(rx_buff.data(), bytes);
    auto result = proto::parse_announce(buff);
    if (!result) {
        spdlog::trace("{}::on_read, cannot parse incoming UDP packet {} bytes from {} :: {}", identity, bytes,
                      peer_endpoint, result.error().message());
    } else {
        auto &msg = result.value();
        auto &sha = msg->id();
        auto device_id = model::device_id_t::from_sha256(sha);
        if (device_id) {
            if (device_id != device->device_id) { // skip "self" discovery via network
                model::peer_contact_t::uri_container_t uris;
                for (int i = 0; i < msg->addresses_size(); ++i) {
                    auto uri = utils::parse(msg->addresses(i).c_str());
                    if (uri) {
                        uris.emplace_back(std::move(uri.value()));
                    }
                }
                if (!uris.empty()) {
                    boost::posix_time::ptime now(boost::posix_time::microsec_clock::local_time());
                    model::peer_contact_t contact{std::move(now), std::move(uris)};
                    spdlog::trace("{}, on_read, local peer = {} ", identity, device_id.value().get_value());
                    send<payload::discovery_notification_t>(coordinator, std::move(device_id.value()),
                                                            std::move(contact), std::move(peer_endpoint));
                } else {
                    spdlog::warn("{}, on_read, no valid uris from: {}", identity, peer_endpoint);
                }
            }
        } else {
            spdlog::warn("{}, on_read, wrong device id, coming from: {}", identity, peer_endpoint);
        }
    }
    return do_read();
}

void local_discovery_actor_t::on_read_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::error("{}, on_read_error, error = {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
}

void local_discovery_actor_t::on_write(size_t) noexcept { resources->release(resource::io); }

void local_discovery_actor_t::on_write_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec != asio::error::operation_aborted) {
        spdlog::error("{}, on_write_error, error = {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
}
