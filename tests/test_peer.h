// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "rotor/supervisor.h"
#include "net/messages.h"
#include "model/device.h"
#include "model/cluster.h"
#include "utils/log.h"
#include "syncspirit-test-export.h"
#include <optional>

namespace syncspirit::test {

namespace r = rotor;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

struct block_response_t {
    std::string name;
    size_t block_index;
    utils::bytes_t data;
    sys::error_code ec;
};

using block_response_opt_t = std::optional<block_response_t>;

struct test_peer_config_t : public r::actor_config_t {
    model::device_ptr_t peer_device;
    r::address_ptr_t coordinator;
    std::string url;
    bool auto_share = false;
    model::cluster_ptr_t cluster;
};

template <typename Actor> struct test_peer_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device(const model::device_ptr_t &value) && noexcept {
        parent_t::config.peer_device = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&coordinator(const r::address_ptr_t &value) && noexcept {
        parent_t::config.coordinator = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&auto_share(bool value) && noexcept {
        parent_t::config.auto_share = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&url(std::string_view value) && noexcept {
        parent_t::config.url = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_TEST_API test_peer_t : r::actor_base_t {
    static const constexpr size_t next_block = 1000000;

    using config_t = test_peer_config_t;
    template <typename Actor> using config_builder_t = test_peer_config_builder_t<Actor>;

    using remote_message_t = r::intrusive_ptr_t<net::message::forwarded_messages_t>;
    using remote_messages_t = std::list<remote_message_t>;
    using bep_messages_t = std::list<net::payload::forwarded_message_t>;
    using shutdown_start_callback_t = std::function<void()>;

    using allowed_index_updates_t = std::unordered_set<std::string>;
    using requests_t = std::list<proto::Request>;
    using responses_t = std::list<proto::Response>;

    test_peer_t(config_t &config);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

    void on_controller_up(net::message::controller_up_t &msg) noexcept;
    void on_controller_predown(net::message::controller_predown_t &msg) noexcept;
    void on_transfer(net::message::transfer_data_t &message) noexcept;
    void process_block_requests() noexcept;
    void forward(net::payload::forwarded_message_t payload) noexcept;

    void push_response(proto::ErrorCode, std::int32_t request_id) noexcept;
    void push_response(utils::bytes_view_t data, std::int32_t request_id) noexcept;

    int blocks_requested = 0;
    bool reading = false;
    bool auto_share = false;
    model::cluster_ptr_t cluster;
    std::string url;
    remote_messages_t raw_messages;
    bep_messages_t bep_messages;
    r::address_ptr_t coordinator;
    r::address_ptr_t controller;
    model::device_ptr_t peer_device;
    model::device_state_t peer_state;
    utils::logger_t log;

    requests_t in_requests;
    requests_t in_requests_copy;
    responses_t out_responses;

    requests_t out_requests;
    responses_t in_responses;

    allowed_index_updates_t allowed_index_updates;
    shutdown_start_callback_t shutdown_start_callback;
};

} // namespace syncspirit::test
