// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test_peer.h"
#include "net/names.h"
#include "model/diff/contact/peer_state.h"
#include "utils/log.h"
#include "proto/bep_support.h"
#include "proto/proto-helpers-bep.h"
#include "model/messages.h"

using namespace syncspirit;
using namespace syncspirit::test;

test_peer_t::test_peer_t(config_t &config)
    : r::actor_base_t{config}, auto_share(config.auto_share), coordinator(config.coordinator),
      peer_device{config.peer_device}, cluster{config.cluster}, url(config.url),
      peer_state{model::device_state_t::make_offline()} {
    auto id = fmt::format("test.peer.{}", url.c_str());
    log = utils::get_logger(id);
    assert(cluster);
    assert(peer_device);
    assert(!url.empty());
}

void test_peer_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        auto id = fmt::format("test.peer.{}", url.c_str());
        p.set_identity(id, false);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&test_peer_t::on_controller_up, coordinator);
                plugin->subscribe_actor(&test_peer_t::on_controller_predown, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&test_peer_t::on_transfer);
    });
}

void test_peer_t::on_start() noexcept {
    r::actor_base_t::on_start();
    peer_state = peer_state.connecting().connected().online(url);
    auto diff = model::diff::contact::peer_state_t::create(*cluster, peer_device->device_id().get_sha256(),
                                                           get_address(), peer_state);
    assert(diff);
    auto sup_addr = supervisor->get_address();
    send<model::payload::model_update_t>(sup_addr, std::move(diff), nullptr);
}

void test_peer_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    if (controller) {
        send<net::payload::peer_down_t>(controller, address, shutdown_reason);
    }
    if (shutdown_start_callback) {
        shutdown_start_callback();
    }
    r::actor_base_t::shutdown_start();
}

void test_peer_t::shutdown_finish() noexcept {
    r::actor_base_t::shutdown_finish();
    LOG_TRACE(log, "shutdown_finish, blocks requested = {}", blocks_requested);
    if (controller) {
        send<net::payload::peer_down_t>(controller, address, shutdown_reason);
    }
    auto sha256 = peer_device->device_id().get_sha256();
    auto diff = model::diff::contact::peer_state_t::create(*cluster, sha256, address, peer_state.offline());
    if (diff) {
        send<model::payload::model_update_t>(coordinator, std::move(diff));
    }
}

void test_peer_t::on_controller_up(net::message::controller_up_t &msg) noexcept {
    auto &p = msg.payload;
    if (p.peer == peer_device->device_id() && p.url->c_str() == url) {
        LOG_TRACE(log, "on_controller_up");
        controller = msg.payload.controller;
        reading = true;
    }
}

void test_peer_t::on_controller_predown(net::message::controller_predown_t &msg) noexcept {
    auto for_me = msg.payload.peer == address;
    LOG_TRACE(log, "on_controller_predown, for_me = {}", (for_me ? "yes" : "no"));
    if (for_me && !shutdown_reason) {
        auto &ee = msg.payload.ee;
        auto reason = ee->message();
        LOG_TRACE(log, "on_termination: {}", reason);

        do_shutdown(ee);
    }
}

void test_peer_t::on_transfer(net::message::transfer_data_t &message) noexcept {
    auto &data = message.payload.data;
    auto buff = utils::bytes_view_t(data);
    while (buff.size()) {
        auto result = proto::parse_bep(buff);
        auto& value = result.value();
        auto sz = value.consumed;
        buff = buff.subspan(sz);
        auto orig = std::move(value).message;
        auto type = proto::MessageType::UNKNOWN;
        auto variant = net::payload::forwarded_message_t();
        std::visit(
            [&](auto &msg) {
                using T = std::decay_t<decltype(msg)>;
                using V = net::payload::forwarded_message_t;
                type = proto::message::get_bep_type<T>();
                if constexpr (std::is_same_v<T, proto::Request>) {
                    in_requests.push_back(std::move(msg));
                    ++blocks_requested;
#if 0
                    if (block_callback) {
                        auto opt = block_callback(this, req);
                        if (opt) {
                            block_responses.emplace_back(*opt);
                        }
                    } else if (block_responses.size()) {
                        log->debug("{}, top response block # {}", identity, block_responses.front().block_index);
                    }
#endif
                    process_block_requests();
                } else if constexpr (std::is_same_v<T, proto::Response>) {
                    in_responses.push_back(std::move(msg));
                } else  if constexpr (std::is_constructible_v<V, T>) {
                    variant = std::move(msg);
                }

            },
            orig);
        LOG_TRACE(log, "on_transfer, bytes = {}, type = {}",  data.size(), (int)type);
        auto fwd_msg = new net::message::forwarded_message_t(address, std::move(variant));
        messages.emplace_back(fwd_msg);

        for (auto &msg : messages) {
            auto &p = msg->payload;
            if (auto m = std::get_if<proto::Index>(&p); m) {
                auto folder = proto::get_folder(*m);
                allowed_index_updates.emplace(std::move(folder));
            }
            if (auto m = std::get_if<proto::IndexUpdate>(&p); m) {
                auto folder = std::string(proto::get_folder(*m));
                if ((allowed_index_updates.count(folder) == 0) && !auto_share) {
                    LOG_WARN(log, "IndexUpdate w/o previously recevied index");
                    std::abort();
                }
            }
        }
    }
}

void test_peer_t::process_block_requests() noexcept {
    auto condition = [&]() -> bool {
        if (in_requests.size() && out_responses.size()) {
            auto &req = in_requests.front();
            auto &res = out_responses.front();
            auto req_id = proto::get_id(req);
            auto res_id = proto::get_id(res);
            if (req_id == res_id) {
                return true;
            }
        }
        return false;
    };
    while (condition()) {
        auto &req = in_requests.front();
        auto &res = out_responses.front();
        auto res_id = proto::get_id(res);
        auto code = proto::get_code(res);
        log->debug("request & responce match by id '{}', replying..., code = {}", res_id, (int)code);
        send<net::payload::forwarded_message_t>(controller, std::move(res));
        in_requests.pop_front();
        out_responses.pop_front();
    }
}

void test_peer_t::push_response(proto::Response res) noexcept {
    out_responses.emplace_back(std::move(res));
}

void test_peer_t::push_response(utils::bytes_view_t data, std::int32_t request_id) noexcept {
    proto::Response res;
    proto::set_id(res, request_id);
    proto::set_data(res, data);
    out_responses.emplace_back(std::move(res));
}

void test_peer_t::forward(net::payload::forwarded_message_t payload) noexcept {
    send<net::payload::forwarded_message_t>(controller, std::move(payload));
}
