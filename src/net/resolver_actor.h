// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "messages.h"
#include "../utils/log.h"
#include <boost/asio.hpp>
#include <rotor.hpp>

namespace syncspirit {
namespace net {

struct endpoint_t {
    std::string host;
    std::string port;
    bool operator==(const endpoint_t &other) const noexcept { return host == other.host && port == other.port; }
};

} // namespace net
} // namespace syncspirit

namespace std {
using endpoint_t = syncspirit::net::endpoint_t;

template <> struct hash<endpoint_t> {
    inline size_t operator()(const endpoint_t &endpoint) const noexcept {
        auto h1 = std::hash<std::string>()(endpoint.host);
        auto h2 = std::hash<std::string>()(endpoint.port);
        return h1 ^ (h2 << 4);
    }
};

} // namespace std

namespace syncspirit {
namespace net {

struct resolver_actor_config_t : public r::actor_config_t {
    r::pt::time_duration resolve_timeout;
    using r::actor_config_t::actor_config_t;
};

template <typename Actor> struct resolver_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&resolve_timeout(const pt::time_duration &value) &&noexcept {
        parent_t::config.resolve_timeout = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct resolver_actor_t : public r::actor_base_t {

    using config_t = resolver_actor_config_t;
    template <typename Actor> using config_builder_t = resolver_actor_config_builder_t<Actor>;

    explicit resolver_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

  private:
    using request_ptr_t = r::intrusive_ptr_t<message::resolve_request_t>;
    using resolve_results_t = payload::address_response_t::resolve_results_t;
    using Queue = std::list<request_ptr_t>;
    using Cache = std::unordered_map<endpoint_t, resolve_results_t>;

    void on_request(message::resolve_request_t &req) noexcept;
    void on_cancel(message::resolve_cancel_t &message) noexcept;
    void mass_reply(const endpoint_t &endpoint, const resolve_results_t &results) noexcept;
    void mass_reply(const endpoint_t &endpoint, const std::error_code &ec) noexcept;
    void process() noexcept;
    void resolve_start(request_ptr_t &req) noexcept;
    void on_resolve(resolve_results_t results) noexcept;
    void on_resolve_error(const sys::error_code &ec) noexcept;
    void on_timer(r::request_id_t, bool cancelled) noexcept;
    void cancel_timer() noexcept;

    template <typename ReplyFn> void reply(const endpoint_t &endpoint, ReplyFn &&fn) noexcept {
        auto it = queue.begin();
        while (it != queue.end()) {
            auto &message_ptr = *it;
            auto &payload = *message_ptr->payload.request_payload;
            if (payload.host == endpoint.host && payload.port == endpoint.port) {
                fn(*message_ptr);
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    }

    utils::logger_t log;
    pt::time_duration io_timeout;
    asio::io_context::strand &strand;
    tcp::resolver backend;
    //asio::deadline_timer timer;
    std::optional<r::request_id_t> timer_id;
    Queue queue;
    Cache cache;
};

} // namespace net
} // namespace syncspirit
