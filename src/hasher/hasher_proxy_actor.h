#pragma once

#include "messages.h"
#include "../utils/log.h"

namespace syncspirit {
namespace hasher {

namespace r = rotor;

struct hasher_proxy_actor_config_t : r::actor_config_t {
    uint32_t hasher_threads;
    std::string name;
};

template <typename Actor> struct hasher_proxy_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&hasher_threads(uint32_t value) &&noexcept {
        parent_t::config.hasher_threads = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&name(std::string_view value) &&noexcept {
        parent_t::config.name = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct hasher_proxy_actor_t : public r::actor_base_t {
    using config_t = hasher_proxy_actor_config_t;
    template <typename Actor> using config_builder_t = hasher_proxy_actor_config_builder_t<Actor>;

    hasher_proxy_actor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

  private:
    using addresses_t = std::vector<r::address_ptr_t>;

    void on_digest_request(hasher::message::digest_request_t &req) noexcept;
    void on_digest_response(hasher::message::digest_response_t &res) noexcept;
    void on_validation_request(hasher::message::validation_request_t &req) noexcept;
    void on_validation_response(hasher::message::validation_response_t &res) noexcept;

    r::address_ptr_t find_next_hasher() noexcept;
    void free_hasher(r::address_ptr_t &addr) noexcept;

    utils::logger_t log;
    addresses_t hashers;
    std::vector<uint32_t> hasher_scores;
    uint32_t hasher_threads;
    std::string name;
    uint32_t index = 0;
};

} // namespace hasher
} // namespace syncspirit
