// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "relay_actor.h"
#include "names.h"
#include "constants.h"
#include "utils/error_code.h"
#include "utils/beast_support.h"
#include "utils/time.h"
#include "model/messages.h"
#include "model/diff/contact/update_contact.h"
#include "model/diff/contact/relay_connect_request.h"
#include "model/diff/contact/unknown_connected.h"
#include "model/diff/modify/add_pending_device.h"
#include "transport/stream.h"
#include <spdlog/fmt/bin_to_hex.h>
#include "messages.h"
#include <cstdlib>
#include <type_traits>
#include <boost/core/demangle.hpp>

using namespace syncspirit::net;

static const constexpr size_t BUFF_SZ = 1500;
template <class> inline constexpr bool always_false_v = false;

namespace {
namespace resource {
r::plugin::resource_id_t init = 0;
r::plugin::resource_id_t http = 1;
r::plugin::resource_id_t io_read = 2;
r::plugin::resource_id_t io_write = 3;
} // namespace resource
} // namespace

relay_actor_t::relay_actor_t(config_t &config) noexcept
    : r::actor_base_t(config), cluster{std::move(config.cluster)}, root_ca{config.root_ca}, config{config.config} {
    http_rx_buff = std::make_shared<payload::http_request_t::rx_buff_t>();
}

void relay_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.relay", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::http11_relay, http_client, true).link(true);
        p.discover_name(names::coordinator, coordinator, false).link(false);
        p.discover_name(names::peer_supervisor, peer_supervisor, true).link(false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&relay_actor_t::on_list);
        p.subscribe_actor(&relay_actor_t::on_connect);
        resources->acquire(resource::init);
        request_relay_list();
    });
}

void relay_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();

    if (resources->has(resource::init)) {
        resources->release(resource::init);
    }
    if (resources->has(resource::http)) {
        send<message::http_cancel_t::payload_t>(http_client, *http_request, get_address());
    }
    if (resources->has(resource::io_read) || resources->has(resource::io_write)) {
        master->cancel();
    }
    if (rx_state) {
        auto self = cluster->get_device();
        utils::uri_container_t uris;
        for (auto &uri : self->get_uris()) {
            if (uri->scheme() != "relay") {
                uris.emplace_back(uri);
            }
        }
        using namespace model::diff;
        auto diff = model::diff::cluster_diff_ptr_t{};
        diff = new contact::update_contact_t(*cluster, self->device_id(), uris);
        send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    }
}

void relay_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
    connect_to_relay();
}

void relay_actor_t::connect_to_relay() noexcept {
    size_t attempts = 0;
    while (++attempts < 10) {
        relay_index = rand() % relays.size();
        auto relay = relays[relay_index];
        if (!relay) {
            continue;
        }
        auto &l = relay->location;
        auto relay_host = relay->uri->host();
        auto relay_port = std::string_view(relay->uri->port());
        LOG_INFO(log, "chosen relay({}) {}:{}, city: {}, country: {}, continent: {}", relay_index, relay_host,
                 relay_port, l.city, l.country, l.continent);

        auto uri = utils::parse(fmt::format("tcp://{}:{}", relay_host, relay_port));
        request<payload::connect_request_t>(peer_supervisor, relay->device_id, std::move(uri),
                                            constants::relay_protocol_name)
            .send(init_timeout);
        return;
    }
    if (attempts >= 10) {
        auto ec = utils::make_error_code(utils::error_code_t::relay_failure);
        do_shutdown(make_error(ec));
    }
}

void relay_actor_t::request_relay_list() noexcept {
    auto timeout = init_timeout * 9 / 10;
    auto &uri = config.discovery_url;
    LOG_TRACE(log, "going to request relays list, timeout = {} ms, via {}:{}", timeout.total_milliseconds(),
              std::string_view(uri->host()), uri->port_number());

    assert(uri->scheme() == "https");
    http::request<http::empty_body> req;
    req.method(http::verb::get);
    req.version(10);
    req.target(uri->encoded_target());
    req.set(http::field::host, uri->host());
    req.set(http::field::connection, "close");

    utils::bytes_t tx_buff;
    auto r = utils::serialize(req, tx_buff);
    if (!r) {
        auto &ec = r.assume_error();
        LOG_WARN(log, "cannot serialize request: {}'", r.assume_error().message());
        return do_shutdown(make_error(ec));
    }
    resources->acquire(resource::http);
    transport::ssl_junction_t ssl{model::device_id_t{}, nullptr, {}};
    http_request = request<payload::http_request_t>(http_client, uri, std::move(tx_buff), http_rx_buff,
                                                    config.rx_buff_size, std::move(ssl), config.debug)
                       .send(timeout);
}

void relay_actor_t::on_list(message::http_response_t &msg) noexcept {
    LOG_TRACE(log, "on_list");
    resources->release(resource::http);

    auto &ee = msg.payload.ee;
    if (ee) {
        LOG_WARN(log, "get public relays failed: {}", ee->message());
        auto inner = utils::make_error_code(utils::error_code_t::cannot_get_public_relays);
        return do_shutdown(make_error(inner, ee));
    }
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto &body = msg.payload.res->response.body();
    auto result = proto::relay::parse_endpoint(body);
    if (!result) {
        auto &ec = result.assume_error();
        LOG_WARN(log, "cannot parse relays: {}", ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &list = result.assume_value();
    if (list.empty()) {
        LOG_WARN(log, "empty list of public relays");
        auto ec = utils::make_error_code(utils::error_code_t::cannot_get_public_relays);
        return do_shutdown(make_error(ec));
    }
    relays = std::move(list);
    http_rx_buff.reset();
    resources->release(resource::init);
}

void relay_actor_t::read_master() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    transport::io_fn_t on_read = [&](auto arg) { this->on_read(arg); };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::io_read); };
    resources->acquire(resource::io_read);
    auto buff = asio::buffer(rx_buff.data() + rx_idx, rx_buff.size() - rx_idx);
    LOG_TRACE(log, "read_master, sz = {}", buff.size());
    master->async_recv(buff, on_read, on_error);
}

void relay_actor_t::push_master(utils::bytes_t data) noexcept {
    tx_queue.emplace_back(tx_item_t(new utils::bytes_t(std::move(data))));
    if (!resources->has(resource::io_write)) {
        write_master();
    }
}

void relay_actor_t::write_master() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    auto &item = tx_queue.front();
    transport::io_fn_t on_write = [&](auto sz) {
        LOG_TRACE(log, "write {} bytes", sz);
        resources->release(resource::io_write);
        tx_queue.pop_front();
        if (tx_timer) {
            cancel_timer(*tx_timer);
        }
        if (!tx_queue.empty()) {
            write_master();
        }
    };
    transport::error_fn_t on_error = [&](auto arg) { this->on_io_error(arg, resource::io_write); };
    auto buff = asio::buffer(*item);
    master->async_send(buff, on_write, on_error);
    resources->acquire(resource::io_write);
    respawn_tx_timer();
}

void relay_actor_t::on_connect(message::connect_response_t &res) noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    LOG_TRACE(log, "on_connect");
    auto &ee = res.payload.ee;
    auto &r = relays[relay_index];
    if (ee) {
        LOG_TRACE(log, "failed to connect to relay {}: {}", r->device_id.get_short(), ee->message());
        relays[relay_index].reset();
        return connect_to_relay();
    }
    LOG_DEBUG(log, "connected to relay {}", r->device_id.get_short());
    auto &p = res.payload.res;
    master = std::move(p.transport);
    master_endpoint = std::move(p.remote_endpoint);
    rx_buff.resize(BUFF_SZ);
    read_master();
    rx_state |= rx_state_t::response;
    respawn_rx_timer();

    auto tx = utils::bytes_t{};
    proto::relay::serialize(proto::relay::join_relay_request_t{}, tx);
    push_master(tx);
}

void relay_actor_t::on_io_error(const sys::error_code &ec, rotor::plugin::resource_id_t resource) noexcept {
    LOG_TRACE(log, "on_io_error: {}", ec.message());
    resources->release(resource);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "on_io_error: {}", ec.message());
        if (state < r::state_t::SHUTTING_DOWN) {
            do_shutdown(make_error(ec));
        }
    }
}

void relay_actor_t::on_read(std::size_t bytes) noexcept {
    enum process_t { stop = 1 << 0, more = 1 << 1, incomplete = 1 << 2 };

    LOG_TRACE(log, "on_read: {} bytes, data: {}", bytes, spdlog::to_hex(rx_buff.begin(), rx_buff.begin() + bytes));
    resources->release(resource::io_read);
    rx_idx += bytes;
    size_t from = 0;
    auto process_op = process_t::more;
    while (process_op == process_t::more && from < rx_idx) {
        auto start = (unsigned char *)(rx_buff.data() + from);
        auto sz = rx_idx - from;
        auto r = proto::relay::parse({start, sz});
        process_op = std::visit(
            [&](auto &it) -> process_t {
                using T = std::decay_t<decltype(it)>;
                if constexpr (std::is_same_v<T, proto::relay::incomplete_t>) {
                    return process_t::incomplete;
                } else if constexpr (std::is_same_v<T, proto::relay::protocol_error_t>) {
                    LOG_ERROR(log, "protocol error (master)");
                    auto ec = utils::make_error_code(utils::error_code_t::protocol_error);
                    do_shutdown(make_error(ec));
                    return process_t::stop;
                } else if constexpr (std::is_same_v<T, proto::relay::wrapped_message_t>) {
                    from += it.length;
                    auto ok = on(it.message);
                    return ok ? process_t::more : process_t::stop;
                } else {
                    static_assert(always_false_v<T>, "non-exhaustive visitor!");
                }
            },
            r);
    }
    rx_idx -= from;
    if (process_op != process_t::stop) {
        read_master();
    }
}

bool relay_actor_t::on(proto::relay::message_t &msg) noexcept {
    return std::visit(
        [&](auto &it) -> bool {
            using boost::core::demangle;
            using T = std::decay_t<decltype(it)>;
            auto message_name = demangle(typeid(T).name());
            LOG_TRACE(log, "received message: {}", message_name);
            bool err = false;
            bool cancel_rx_timer = false;
            if constexpr (std::is_same_v<T, proto::relay::pong_t>) {
                if (rx_state & rx_state_t::pong) {
                    rx_state = rx_state & ~rx_state_t::pong;
                    cancel_rx_timer = true;
                } else {
                    err = true;
                }
            } else if constexpr (std::is_same_v<T, proto::relay::ping_t>) {
                auto tx = utils::bytes_t{};
                proto::relay::serialize(proto::relay::pong_t{}, tx);
                push_master(tx);
            } else if constexpr (std::is_same_v<T, proto::relay::response_t>) {
                if (rx_state & rx_state_t::response) {
                    cancel_rx_timer = true;
                    rx_state = rx_state & ~rx_state_t::response;
                    err = !on(it);
                } else {
                    err = true;
                }
            } else if constexpr (std::is_same_v<T, proto::relay::session_invitation_t>) {
                if (rx_state & rx_state_t::invitation) {
                    return on(it);
                } else {
                    err = true;
                }
            } else {
                err = true;
            }
            if (err) {
                LOG_ERROR(log, "protocol error (master, unexpected message): {}", message_name);
                auto ec = utils::make_error_code(utils::error_code_t::protocol_error);
                do_shutdown(make_error(ec));
            }
            if (cancel_rx_timer && rx_timer) {
                cancel_timer(*rx_timer);
                rx_timer.reset();
            }
            return !err;
        },
        msg);
}

bool relay_actor_t::on(proto::relay::response_t &res) noexcept {
    LOG_DEBUG(log, "on response code = {}", res.code);
    if (res.code) {
        LOG_WARN(log, "response error, details = {}", res.details);
        auto ec = utils::make_error_code(utils::error_code_t::relay_failure);
        do_shutdown(make_error(ec));
        return false;
    }
    respawn_ping_timer();
    rx_state |= rx_state_t::invitation;
    auto self = cluster->get_device();
    auto uris = self->get_uris();
    uris.emplace_back(relays[relay_index]->uri);
    using namespace model::diff;
    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new contact::update_contact_t(*cluster, self->device_id(), uris);
    send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    return true;
}

bool relay_actor_t::on(proto::relay::session_invitation_t &msg) noexcept {
    auto diff = model::diff::cluster_diff_ptr_t{};
    auto device_opt = model::device_id_t::from_sha256(msg.from);
    if (!device_opt) {
        LOG_ERROR(log, "not valid device: {}", spdlog::to_hex(msg.from.begin(), msg.from.end()));
        auto ec = utils::make_error_code(utils::error_code_t::invalid_deviceid);
        do_shutdown(make_error(ec));
        return false;
    }

    asio::ip::tcp::endpoint relay_ep;
    if (msg.address.has_value()) {
        sys::error_code ec;
        relay_ep = asio::ip::tcp::endpoint{msg.address.value(), (uint16_t)msg.port};
    } else {
        relay_ep = asio::ip::tcp::endpoint{master_endpoint.address(), (uint16_t)msg.port};
    }

    auto &device_id = device_opt.value();
    if (cluster->get_devices().by_sha256(device_id.get_sha256())) {
        diff = new model::diff::contact::relay_connect_request_t(std::move(device_id), std::move(msg.key),
                                                                 std::move(relay_ep));
    } else {
        auto last_seen = utils::as_seconds(pt::microsec_clock::local_time());
        db::SomeDevice db;
        db::set_name(db, device_id.get_short());
        db::set_last_seen(db, last_seen);
        diff = new model::diff::modify::add_pending_device_t(device_id, db);
        diff->assign_sibling(new model::diff::contact::unknown_connected_t(*cluster, device_id, std::move(db)));
    }

    send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    return true;
}

void relay_actor_t::respawn_ping_timer() noexcept {
    if (ping_timer) {
        cancel_timer(*ping_timer);
    }
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    ping_timer = start_timer(relays[relay_index]->ping_interval, *this, &relay_actor_t::on_ping_timer);
}

void relay_actor_t::respawn_tx_timer() noexcept {
    if (tx_timer) {
        cancel_timer(*tx_timer);
    }
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    tx_timer = start_timer(relays[relay_index]->ping_interval, *this, &relay_actor_t::on_tx_timer);
}

void relay_actor_t::respawn_rx_timer() noexcept {
    if (rx_timer) {
        cancel_timer(*rx_timer);
    }
    if (state > r::state_t::OPERATIONAL) {
        return;
    }
    rx_timer = start_timer(relays[relay_index]->ping_interval, *this, &relay_actor_t::on_rx_timer);
}

void relay_actor_t::on_ping_timer(r::request_id_t, bool cancelled) noexcept {
    ping_timer.reset();
    if (!cancelled) {
        send_ping();
    }
}

void relay_actor_t::on_tx_timer(r::request_id_t, bool cancelled) noexcept {
    tx_timer.reset();
    if (!cancelled) {
        auto ec = utils::make_error_code(utils::error_code_t::tx_timeout);
        do_shutdown(make_error(ec));
    }
}

void relay_actor_t::on_rx_timer(r::request_id_t, bool cancelled) noexcept {
    rx_timer.reset();
    if (!cancelled) {
        auto ec = utils::make_error_code(utils::error_code_t::rx_timeout);
        do_shutdown(make_error(ec));
    }
}

void relay_actor_t::send_ping() noexcept {
    if (state > r::state_t::OPERATIONAL) {
        return;
    }

    auto buff = utils::bytes_t{};
    proto::relay::serialize(proto::relay::ping_t{}, buff);
    push_master(std::move(buff));
    rx_state |= rx_state_t::pong;
}
