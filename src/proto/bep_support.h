#pragma once
#include <fmt/fmt.h>
#include <boost/outcome.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <variant>
#include <vector>
#include "bep.pb.h"
#include "../utils/uri.h"

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;

namespace payload {
using URIs = std::vector<utils::URI>;
}

namespace message {

template <typename Message> using as_pointer = std::unique_ptr<Message>;

using Hello = as_pointer<syncspirit::proto::Hello>;
using ClusterConfig = as_pointer<syncspirit::proto::ClusterConfig>;
using Index = as_pointer<syncspirit::proto::Index>;
using IndexUpdate = as_pointer<syncspirit::proto::IndexUpdate>;
using Request = as_pointer<syncspirit::proto::Request>;
using Response = as_pointer<syncspirit::proto::Response>;
using DownloadProgress = as_pointer<syncspirit::proto::DownloadProgress>;
using Ping = as_pointer<syncspirit::proto::Ping>;
using Close = as_pointer<syncspirit::proto::Close>;

using Announce = as_pointer<syncspirit::proto::Announce>; // separate, not part of BEP

using message_t =
    std::variant<Hello, ClusterConfig, Index, IndexUpdate, Request, Response, DownloadProgress, Ping, Close>;

struct wrapped_message_t {
    message_t message;
    std::size_t consumed = 0;
};

} // namespace message

void make_hello_message(fmt::memory_buffer &buff, const std::string_view &device_name) noexcept;

std::size_t make_announce_message(fmt::memory_buffer &buff, const std::string_view &device_name,
                                  const payload::URIs &uris, std::int64_t instance) noexcept;

outcome::result<message::wrapped_message_t> parse_bep(const asio::const_buffer &buff) noexcept;

outcome::result<message::Announce> parse_announce(const asio::const_buffer &buff) noexcept;

} // namespace syncspirit::proto
