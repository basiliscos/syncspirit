// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "messages.h"
#include "utils/log.h"
#include "utils/dns.h"
#include <boost/asio.hpp>
#include <rotor.hpp>
#include <ares.h>

namespace syncspirit {
namespace net {

struct resolver_actor_config_t : public r::actor_config_t {
    using r::actor_config_t::actor_config_t;
    r::pt::time_duration resolve_timeout;
    std::string_view hosts_path;
    std::string_view resolvconf_path;
};

template <typename Actor> struct resolver_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&resolve_timeout(const pt::time_duration &value) && noexcept {
        parent_t::config.resolve_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&hosts_path(std::string_view value) && noexcept {
        parent_t::config.hosts_path = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&resolvconf_path(std::string_view value) && noexcept {
        parent_t::config.resolvconf_path = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API resolver_actor_t : public r::actor_base_t {

    using config_t = resolver_actor_config_t;
    template <typename Actor> using config_builder_t = resolver_actor_config_builder_t<Actor>;

    explicit resolver_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void do_initialize(r::system_context_t *ctx) noexcept override;
    void on_start() noexcept override;
    void shutdown_finish() noexcept override;

  private:
    using socket_ptr_t = std::unique_ptr<udp_socket_t>;
    using request_ptr_t = r::intrusive_ptr_t<message::resolve_request_t>;
    using resolve_results_t = payload::address_response_t::resolve_results_t;
    using Queue = std::list<request_ptr_t>;
    using Cache = std::unordered_map<utils::dns_query_t, resolve_results_t>;

    void on_request(message::resolve_request_t &req) noexcept;
    void on_cancel(message::resolve_cancel_t &message) noexcept;
    void mass_reply(const utils::dns_query_t &query, const resolve_results_t &results, bool update_cache) noexcept;
    void mass_reply(const utils::dns_query_t &query, const std::error_code &ec) noexcept;
    void process() noexcept;
    void resolve_start(request_ptr_t &req) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void cancel_timer() noexcept;
    bool resolve_as_ip(const utils::dns_query_t &query) noexcept;
    bool resolve_locally(const utils::dns_query_t &query) noexcept;
    void on_read(size_t bytes) noexcept;
    void on_read_error(const sys::error_code &ec) noexcept;
    void on_write(size_t bytes) noexcept;
    void on_write_error(const sys::error_code &ec) noexcept;

    template <typename ReplyFn> void reply(const utils::dns_query_t &query, ReplyFn &&fn) noexcept {
        auto it = queue.begin();
        while (it != queue.end()) {
            auto &message_ptr = *it;
            auto &payload = *message_ptr->payload.request_payload;
            if (query == payload) {
                fn(*message_ptr);
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    }

    utils::logger_t log;
    pt::time_duration io_timeout;
    std::string_view hosts_path;
    std::string_view resolvconf_path;
    asio::io_context::strand &strand;
    std::optional<r::request_id_t> timer_id;
    socket_ptr_t sock;
    ares_channel_t *channel;
    Queue queue;
    Cache cache;
    request_ptr_t current_query;
    fmt::memory_buffer rx_buff;
    unsigned char *tx_buff;
};

} // namespace net
} // namespace syncspirit
