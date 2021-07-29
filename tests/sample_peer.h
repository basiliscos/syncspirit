#pragma once

#include <functional>
#include "rotor.hpp"
#include "net/messages.h"


namespace syncspirit::test {

namespace r = rotor;
namespace message = syncspirit::net::message;

struct sample_peer_t : r::actor_base_t {
    using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;
    using Responses = std::list<std::string>;

    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start_reading(message::start_reading_t &) noexcept;
    void on_block_request(message::block_request_t &req) noexcept;

    Responses responses;
    size_t start_reading = 0;
    configure_callback_t configure_callback;
};


}
