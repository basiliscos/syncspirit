// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "global_discovery_actor.h"
#include "names.h"
#include <nlohmann/json.hpp>
#include "proto/discovery_support.h"
#include "utils/error_code.h"
#include "model/diff/contact/update_contact.h"
#include "model/diff/contact/peer_state.h"
#include "utils/format.hpp"

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t timer = 0;
r::plugin::resource_id_t http = 1;
} // namespace resource
} // namespace

global_discovery_actor_t::global_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, announce_url{cfg.announce_url}, lookup_url{cfg.lookup_url}, ssl_pair{*cfg.ssl_pair},
      rx_buff_size{cfg.rx_buff_size}, io_timeout(cfg.io_timeout), debug{cfg.debug}, cluster{cfg.cluster} {}

void global_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        addr_announce = p.create_address();
        addr_discovery = p.create_address();
        p.set_identity("net.global_discovery", false);
        log = utils::get_logger(identity);

        auto announce_params = announce_url->encoded_params();
        auto announce_id = announce_params.find("id");
        if (announce_id != announce_params.end()) {
            auto device_opt = model::device_id_t::from_string(announce_id->value);
            if (device_opt) {
                announce_device_id = *device_opt;
            } else {
                log->warn("malformed announce device id, no peer check will be performed");
            }
        }
        announce_url->params().clear();

        auto lookup_params = lookup_url->encoded_params();
        auto lookup_id = lookup_params.find("id");
        if (lookup_id != lookup_params.end()) {
            auto device_opt = model::device_id_t::from_string(lookup_id->value);
            if (device_opt) {
                lookup_device_id = *device_opt;
            } else {
                log->warn("malformed lookup device id, no peer check will be performed");
            }
        }
        lookup_url->params().clear();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::http11_gda, http_client, true).link(true);
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&global_discovery_actor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&global_discovery_actor_t::on_announce_response, addr_announce);
        p.subscribe_actor(&global_discovery_actor_t::on_discovery_response, addr_discovery);
    });
}

void global_discovery_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start (addr = {})", (void *)address.get());
    announce();
    r::actor_base_t::on_start();
}

void global_discovery_actor_t::announce() noexcept {
    LOG_TRACE(log, "announce");

    auto &uris = cluster->get_device()->get_uris();
    if (uris.empty()) {
        return;
    }

    bool has_new = false;
    for (auto &uri : uris) {
        if (!announced_uris.count(uri->buffer())) {
            has_new = true;
            break;
        }
    }
    if (!has_new) {
        return;
    }

    for (auto &uri : uris) {
        announced_uris.emplace(uri->buffer());
    }
    if (log->level() <= spdlog::level::debug) {
        std::string joint_uris;
        for (size_t i = 0; i < uris.size(); ++i) {
            joint_uris += uris[i]->buffer();
            if (i + 1 < uris.size()) {
                joint_uris += ", ";
            }
        }
        LOG_DEBUG(log, "announcing accessibility via {}", joint_uris);
    }

    utils::bytes_t tx_buff;
    auto res = proto::make_announce_request(tx_buff, announce_url, uris);
    if (!res) {
        LOG_TRACE(log, "error making announce request :: {}", res.error().message());
        return do_shutdown(make_error(res.error()));
    }
    make_request(addr_announce, res.value(), announce_device_id, std::move(tx_buff));
}

void global_discovery_actor_t::on_announce_response(message::http_response_t &message) noexcept {
    LOG_TRACE(log, "on_announce_response, req = {}", message.payload.request_id());
    resources->release(resource::http);
    http_request.reset();

    auto &ee = message.payload.ee;
    if (ee) {
        LOG_ERROR(log, "announcing error = {}", ee->message());
        auto inner = utils::make_error_code(utils::error_code_t::announce_failed);
        return do_shutdown(make_error(inner, ee));
    }

    auto res = proto::parse_announce(message.payload.res->response);
    if (!res) {
        LOG_WARN(log, "parsing announce error = {}", res.error().message());
        return do_shutdown(make_error(res.error()));
    }

    if (state >= r::state_t::SHUTTING_DOWN)
        return;

    auto reannounce = res.value();
    LOG_DEBUG(log, "will reannounce after {} seconds", reannounce);

    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }

    auto timeout = pt::seconds(reannounce);
    timer_request = start_timer(timeout, *this, &global_discovery_actor_t::on_timer);
    resources->acquire(resource::timer);

    if (!announced) {
        send<payload::announce_notification_t>(coordinator, get_address());
        announced = true;
    }
}

void global_discovery_actor_t::on_discovery_response(message::http_response_t &message) noexcept {
    using namespace model::diff;
    LOG_TRACE(log, "on_discovery_response");
    resources->release(resource::http);
    http_request.reset();
    auto &custom = message.payload.req->payload.request_payload->custom;
    auto msg = static_cast<model::message::model_update_t *>(custom.get());
    auto diff = static_cast<model::diff::contact::peer_state_t *>(msg->payload.diff.get());
    auto sha256 = diff->peer_id;
    auto it = discovering_devices.find(sha256);
    discovering_devices.erase(it);

    auto peer = cluster->get_devices().by_sha256(sha256);
    if (!peer) {
        auto device_id = model::device_id_t::from_sha256(sha256).value();
        LOG_DEBUG(log, "on_discovery_response, device '{}' is no longer exist", device_id);
        return;
    }

    auto &device_id = peer->device_id();
    auto &state = peer->get_state();
    if (!state.is_discovering()) {
        LOG_DEBUG(log, "device '{}' is no longer discovering, ignoring response", device_id);
        return;
    }

    auto reply_diff = model::diff::cluster_diff_ptr_t{};
    auto &ee = message.payload.ee;
    bool found = false;
    if (ee) {
        LOG_WARN(log, "discovery failed = {}", ee->message());
    } else {
        auto &http_res = message.payload.res->response;
        auto res = proto::parse_contact(http_res);
        if (!res) {
            auto reason = res.error().message();
            auto &body = http_res.body();
            LOG_WARN(log, "parsing discovery error = {}, body({}):\n {}", reason, body.size(), body);
        } else {
            auto &uris = res.value();
            if (!uris.empty()) {
                LOG_DEBUG(log, "on_discovery_response, found some URIs for {}", device_id);
                found = true;
                reply_diff = new contact::update_contact_t(*cluster, peer->device_id(), uris);
            } else {
                LOG_DEBUG(log, "on_discovery_response, no known URIs for {}", device_id);
            }
        }
    }
    auto change_state_diff = model::diff::contact::peer_state_t::create(*cluster, sha256, {}, state.offline());
    assert(change_state_diff);
    if (reply_diff) {
        reply_diff->assign_sibling(change_state_diff.get());
    } else {
        reply_diff = std::move(change_state_diff);
    }
    send<model::payload::model_update_t>(coordinator, std::move(reply_diff));
}

void global_discovery_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, &message);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void global_discovery_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    announced_uris.clear();
    if (!cancelled && (state == r::state_t::OPERATIONAL)) {
        announce();
    }
}

void global_discovery_actor_t::make_request(const r::address_ptr_t &addr, utils::uri_ptr_t &uri,
                                            const model::device_id_t &device_id, utils::bytes_t &&tx_buff,
                                            const custom_msg_ptr_t &custom) noexcept {
    auto timeout = r::pt::millisec{io_timeout};
    transport::ssl_junction_t ssl{device_id, &ssl_pair, "http/1.1"};
    auto rx_buff = std::make_shared<rx_buff_t::element_type>(rx_buff_size);
    http_request = request_via<payload::http_request_t>(http_client, addr, uri, std::move(tx_buff), std::move(rx_buff),
                                                        rx_buff_size, std::move(ssl), debug, custom)
                       .send(timeout);
    resources->acquire(resource::http);
}

void global_discovery_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    if (resources->has(resource::http)) {
        send<message::http_cancel_t::payload_t>(http_client, *http_request, get_address());
    }
    send<payload::http_close_connection_t>(http_client);

    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }
    r::actor_base_t::shutdown_start();
}

auto global_discovery_actor_t::operator()(const model::diff::contact::update_contact_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.self && state == r::state_t::OPERATIONAL) {
        if (resources->has(resource::timer)) {
            cancel_timer(*timer_request);
        }
        announce();
    }
    return diff.visit_next(*this, custom);
}

auto global_discovery_actor_t::operator()(const model::diff::contact::peer_state_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto sha256 = diff.peer_id;
    auto peer = cluster->get_devices().by_sha256(sha256);
    if ((state != r::state_t::OPERATIONAL) || !peer || !peer->get_state().is_unknown()) {
        return diff.visit_next(*this, custom);
    }

    if (discovering_devices.count(sha256)) {
        auto device_id = model::device_id_t::from_sha256(sha256).value();
        LOG_TRACE(log, "device '{}' is already discovering, skip", device_id.get_short());
        return diff.visit_next(*this, custom);
    }

    if (!peer) {
        LOG_ERROR(log, "no peer device '{}'", peer->device_id());
    } else {
        utils::bytes_t tx_buff;
        auto r = proto::make_discovery_request(tx_buff, lookup_url, peer->device_id());
        if (!r) {
            LOG_ERROR(log, "error making discovery request: {}", r.error().message());
            return r.error();
        }

        discovering_devices.emplace(sha256);
        auto msg = reinterpret_cast<model::message::model_update_t *>(custom);
        make_request(addr_discovery, r.value(), lookup_device_id, std::move(tx_buff), msg);
        auto diff = model::diff::contact::peer_state_t::create(*cluster, sha256, {}, peer->get_state().discover());
        assert(diff);
        send<model::payload::model_update_t>(coordinator, std::move(diff));
    }
    return diff.visit_next(*this, custom);
}
