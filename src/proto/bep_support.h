#pragma once
#include <fmt/format.h>
#include <boost/outcome.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include "bep.pb.h"

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;

namespace message {

template <typename Message> struct message_t {
    std::unique_ptr<Message> message;
    std::size_t consumed = 0;
};

using Hello = message_t<syncspirit::proto::Hello>;
} // namespace message

void make_hello_message(fmt::memory_buffer &buff, const std::string_view &device_name) noexcept;

outcome::result<message::Hello> parse_hello(asio::mutable_buffer buff) noexcept;

} // namespace syncspirit::proto
