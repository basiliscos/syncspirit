// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "local_discovery_actor.h"
#include "names.h"
#include "proto/bep_support.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "utils/time.h"
#include "model/diff/contact/update_contact.h"
#include "model/diff/contact/ignored_connected.h"
#include "model/diff/contact/unknown_connected.h"
#include "model/diff/modify/add_unknown_device.h"
#include "model/messages.h"

using namespace syncspirit;
using namespace syncspirit::net;

static const constexpr std::size_t BUFF_SZ = 1500;

namespace {
namespace resource {
r::plugin::resource_id_t send = 0;
r::plugin::resource_id_t read = 1;
r::plugin::resource_id_t timer = 2;
} // namespace resource
} // namespace

local_discovery_actor_t::local_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, frequency{r::pt::milliseconds(cfg.frequency)},
      strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()}, broadcast_sock{strand.context()},
      sock{strand.context()}, port{cfg.port}, cluster{cfg.cluster} {
    rx_buff.resize(BUFF_SZ);
    tx_buff.resize(BUFF_SZ);
}

void local_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.local_discovery", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::coordinator, coordinator, true).link(false); });
}

void local_discovery_actor_t::init() noexcept {
    LOG_TRACE(log, "init, will announce to port = {}", port);

    sys::error_code ec;

    auto bc_endpoint = udp::endpoint(asio::ip::address_v4::broadcast(), port);
    broadcast_sock.open(bc_endpoint.protocol(), ec);
    if (ec) {
        LOG_WARN(log, "init, can't open broadcast socket :: {}", ec.message());
        return do_shutdown(make_error(ec));
    }

    broadcast_sock.set_option(udp_socket_t::broadcast(true), ec);
    if (ec) {
        LOG_WARN(log, "init, can't set broadcast option :: {}", ec.message());
        return do_shutdown(make_error(ec));
    }

    auto listen_endpoint = udp::endpoint{asio::ip::address_v4::loopback(), port};
    sock.open(listen_endpoint.protocol(), ec);
    if (ec) {
        LOG_WARN(log, "init, can't open socket :: {}", ec.message());
        return do_shutdown(make_error(ec));
    }

    sock.bind(listen_endpoint, ec);
    if (ec) {
        LOG_WARN(log, "init, can't bind socket {} :: {}", listen_endpoint, ec.message());
        return do_shutdown(make_error(ec));
    }
}

void local_discovery_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
    init();
    do_read();
    announce();
}

void local_discovery_actor_t::shutdown_start() noexcept {
    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }
    sys::error_code ec;
    if (resources->has(resource::send)) {
        broadcast_sock.cancel(ec);
        if (ec) {
            LOG_WARN(log, "shutdown_start, socket cancellation error:: {}", ec.message());
        }
    }
    if (resources->has(resource::read)) {
        sock.cancel(ec);
        if (ec) {
            LOG_WARN(log, "shutdown_start, socket cancellation error:: {}", ec.message());
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
        broadcast_sock.async_send_to(buff, bc_endpoint, std::move(fwd_send));
        resources->acquire(resource::send);
        LOG_TRACE(log, "announce has been sent");
    } else {
        LOG_TRACE(log, "announce() skipping");
    }

    timer_request = start_timer(frequency, *this, &local_discovery_actor_t::on_timer);
    resources->acquire(resource::timer);
}

void local_discovery_actor_t::do_read() noexcept {
    auto buff = asio::buffer(rx_buff.data(), BUFF_SZ);
    auto fwd_receive =
        ra::forwarder_t(*this, &local_discovery_actor_t::on_read, &local_discovery_actor_t::on_read_error);
    sock.async_receive_from(buff, peer_endpoint, std::move(fwd_receive));
    resources->acquire(resource::read);
}

void local_discovery_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    timer_request.reset();

    if (!cancelled) {
        announce();
    }
}

void local_discovery_actor_t::on_read(size_t bytes) noexcept {
    resources->release(resource::read);
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    // LOG_TRACE(log, "local_discovery_actor_t::on_read");
    auto buff = asio::buffer(rx_buff.data(), bytes);
    auto result = proto::parse_announce(buff);
    if (!result) {
        LOG_TRACE(log, "on_read, cannot parse incoming UDP packet {} bytes from {} :: {}", bytes, peer_endpoint,
                  result.error().message());
    } else {
        auto &msg = result.value();
        auto &sha = msg->id();
        auto device_id = model::device_id_t::from_sha256(sha);
        if (device_id) {
            utils::uri_container_t uris;
            for (int i = 0; i < msg->addresses_size(); ++i) {
                auto uri = utils::parse(msg->addresses(i).c_str());
                if (uri && uri->has_port()) {
                    uris.emplace_back(std::move(uri));
                }
            }
            if (!uris.empty()) {
                LOG_TRACE(log, "on_read, local peer = {} ", device_id.value().get_value());
                for (auto &uri : uris) {
                    LOG_TRACE(log, "on_read, peer is available via {}", uri);
                }

                using namespace model::diff;
                auto diff = model::diff::contact_diff_ptr_t{};
                diff = new contact::update_contact_t(*cluster, device_id.value(), uris);
                send<model::payload::contact_update_t>(coordinator, std::move(diff), this);
            } else {
                LOG_WARN(log, "on_read, no valid uris from: {}", peer_endpoint);
            }
        } else {
            LOG_WARN(log, "on_read, wrong device id, coming from: {}", peer_endpoint);
        }
    }
    return do_read();
}

void local_discovery_actor_t::on_read_error(const sys::error_code &ec) noexcept {
    resources->release(resource::read);
    if (ec != asio::error::operation_aborted) {
        LOG_ERROR(log, "on_read_error, error = {}", ec.message());
        do_shutdown(make_error(ec));
    }
}

void local_discovery_actor_t::on_write(size_t) noexcept { resources->release(resource::send); }

void local_discovery_actor_t::on_write_error(const sys::error_code &ec) noexcept {
    resources->release(resource::send);
    if (ec != asio::error::operation_aborted) {
        LOG_ERROR(log, "on_write_error, error = {}", ec.message());
        do_shutdown(make_error(ec));
    }
}

struct filler_t {
    template <typename T> static auto fill(T &peer, const std::string &uris_str) -> db::SomeDevice {
        db::SomeDevice db;
        peer->serialize(db);
        db.set_address(uris_str);
        db.set_last_seen(utils::as_seconds(pt::microsec_clock::local_time()));
        return db;
    }
};

void local_discovery_actor_t::handle(const model::device_id_t &device_id, utils::uri_container_t &uris) noexcept {
    if (device_id == cluster->get_device()->device_id()) { // skip "self" discovery via network
        LOG_TRACE(log, "skipping self discovery (?)");
        return;
    }
    if (uris.empty()) {
        LOG_TRACE(log, "no valid urls found for {}", device_id);
        return;
    }

    using namespace model::diff;
    auto diff = model::diff::contact_diff_ptr_t{};
    auto uris_str = fmt::format("{}", fmt::join(uris, ", "));
    LOG_TRACE(log, "on_read, peer is available via {}", uris_str);

    if (auto peer = cluster->get_devices().by_sha256(device_id.get_sha256()); peer) {
        LOG_DEBUG(log, "device '{}' contacted", device_id);
        diff = new contact::update_contact_t(*cluster, device_id, uris);
    } else if (auto peer = cluster->get_ignored_devices().by_sha256(device_id.get_sha256()); peer) {
        LOG_DEBUG(log, "ignored device '{}' contacted", device_id);
        auto db = filler_t::fill(peer, uris_str);
        diff = new contact::ignored_connected_t(*cluster, device_id, std::move(db));
    } else if (auto peer = cluster->get_unknown_devices().by_sha256(device_id.get_sha256()); peer) {
        auto db = filler_t::fill(peer, uris_str);
        diff = new contact::unknown_connected_t(*cluster, device_id, std::move(db));
    } else {
        db::SomeDevice db;
        db.set_name(std::string(device_id.get_short()));
        db.set_address(uris_str);
        db.set_last_seen(utils::as_seconds(pt::microsec_clock::local_time()));
        auto cluster_diff = cluster_diff_ptr_t{};
        cluster_diff = new model::diff::modify::add_unknown_device_t(device_id, db);
        send<model::payload::model_update_t>(coordinator, std::move(cluster_diff));
        diff = new contact::unknown_connected_t(*cluster, device_id, std::move(db));
    }
    if (diff) {
        send<model::payload::contact_update_t>(coordinator, std::move(diff), this);
    }
}
