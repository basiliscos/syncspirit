#pragma once

#include <functional>
#include "rotor.hpp"
#include "net/messages.h"
#include "utils/log.h"

namespace syncspirit::test {

namespace r = rotor;
namespace message = syncspirit::net::message;

struct sample_peer_t : r::actor_base_t {
    using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;
    using request_ptr_t = r::intrusive_ptr_t<message::block_request_t>;

    struct response_t {
        size_t block_index;
        std::string data;
    };
    static const constexpr size_t next_block = 1000000;

    using responses_t = std::list<response_t>;
    using requests_t = std::list<request_ptr_t>;
    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start_reading(message::start_reading_t &) noexcept;
    void on_block_request(message::block_request_t &req) noexcept;
    void push_response(const std::string &data, size_t index = next_block) noexcept;

    utils::logger_t log;
    responses_t responses;
    size_t start_reading = 0;
    configure_callback_t configure_callback;
    requests_t requests;
};

} // namespace syncspirit::test
