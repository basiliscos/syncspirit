#pragma once
#include <fmt/format.h>
#include <boost/outcome.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <variant>
#include "bep.pb.h"

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;

namespace message {

template <typename Message> using as_pointer = std::unique_ptr<Message>;

using Hello = as_pointer<syncspirit::proto::Hello>;

using message_t = std::variant<Hello>;

struct wrapped_message_t {
    message_t message;
    std::size_t consumed = 0;
};

} // namespace message

void make_hello_message(fmt::memory_buffer &buff, const std::string_view &device_name) noexcept;

outcome::result<message::wrapped_message_t> parse_bep(const asio::const_buffer &buff) noexcept;

} // namespace syncspirit::proto
