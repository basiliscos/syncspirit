// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "config/bep.h"
#include "proto/bep_support.h"
#include "utils/log.h"
#include "model/cluster.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>
#include <optional>
#include <list>
#include <chrono>

namespace syncspirit {

namespace net {

struct peer_actor_config_t : public r::actor_config_t {
    std::string_view device_name;
    model::device_id_t peer_device_id;
    config::bep_config_t bep_config;
    transport::stream_sp_t transport;
    r::address_ptr_t coordinator;
    model::cluster_ptr_t cluster;
    tcp::endpoint peer_endpoint;
    std::string peer_proto;
};

template <typename Actor> struct peer_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) && noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&device_name(std::string_view value) && noexcept {
        parent_t::config.device_name = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&bep_config(const config::bep_config_t &value) && noexcept {
        parent_t::config.bep_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&coordinator(const r::address_ptr_t &value) && noexcept {
        parent_t::config.coordinator = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&transport(transport::stream_sp_t value) && noexcept {
        parent_t::config.transport = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&peer_endpoint(const tcp::endpoint &value) && noexcept {
        parent_t::config.peer_endpoint = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&peer_proto(std::string value) && noexcept {
        parent_t::config.peer_proto = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API peer_actor_t : public r::actor_base_t {
    using config_t = peer_actor_config_t;
    template <typename Actor> using config_builder_t = peer_actor_config_builder_t<Actor>;

    peer_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;
    void shutdown_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    struct confidential {
        struct payload {
            struct tx_item_t : boost::intrusive_ref_counter<tx_item_t, boost::thread_unsafe_counter> {
                utils::bytes_t buff;
                bool final = false;

                tx_item_t(utils::bytes_t buff_, bool final_) noexcept : buff{std::move(buff_)}, final{final_} {}
                tx_item_t(tx_item_t &&other) = default;
            };
        };

        struct message {
            using tx_item_t = r::message_t<payload::tx_item_t>;
        };
    };

    using tx_item_t = model::intrusive_ptr_t<confidential::payload::tx_item_t>;
    using tx_message_t = confidential::message::tx_item_t;
    using tx_queue_t = std::list<tx_item_t>;
    using read_action_t = void (peer_actor_t::*)(proto::message::message_t &&msg);
    using block_request_ptr_t = r::intrusive_ptr_t<message::block_request_t>;
    using block_requests_t = std::list<block_request_ptr_t>;
    using clock_t = std::chrono::steady_clock;

    void on_controller_up(message::controller_up_t &) noexcept;
    void on_controller_predown(message::controller_predown_t &) noexcept;
    void on_block_request(message::block_request_t &) noexcept;
    void on_transfer(message::transfer_data_t &message) noexcept;

    void on_io_error(const sys::error_code &ec, r::plugin::resource_id_t resource) noexcept;
    void on_write(std::size_t bytes) noexcept;
    void on_read(std::size_t bytes) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void read_more() noexcept;
    void push_write(utils::bytes_t buff, bool signal, bool final) noexcept;
    void process_tx_queue() noexcept;
    void cancel_timer() noexcept;
    void cancel_io() noexcept;
    void on_tx_timeout(r::request_id_t, bool cancelled) noexcept;
    void on_rx_timeout(r::request_id_t, bool cancelled) noexcept;

    void reset_tx_timer() noexcept;
    void reset_rx_timer() noexcept;
    void read_hello(proto::message::message_t &&msg) noexcept;
    void read_controlled(proto::message::message_t &&msg) noexcept;

    void handle_hello(proto::Hello &&) noexcept;
    void handle_ping(proto::Ping &&) noexcept;
    void handle_close(proto::Close &&) noexcept;
    void handle_response(proto::Response &&) noexcept;

    void emit_io_stats(bool force = false) noexcept;

    model::cluster_ptr_t cluster;
    utils::logger_t log;
    std::string_view device_name;
    config::bep_config_t bep_config;
    r::address_ptr_t coordinator;
    model::device_id_t peer_device_id;
    transport::stream_sp_t transport;
    std::optional<r::request_id_t> timer_request;
    std::optional<r::request_id_t> tx_timer_request;
    std::optional<r::request_id_t> rx_timer_request;
    tx_queue_t tx_queue;
    tx_item_t tx_item;
    fmt::memory_buffer rx_buff;
    std::size_t rx_idx = 0;
    std::string cert_name;
    tcp::endpoint peer_endpoint;
    std::string peer_proto;
    read_action_t read_action;
    r::address_ptr_t controller;
    block_requests_t block_requests;
    std::size_t rx_bytes;
    std::size_t tx_bytes;
    clock_t::time_point last_stats;
    bool finished = false;
    bool io_error = false;
};

} // namespace net
} // namespace syncspirit
