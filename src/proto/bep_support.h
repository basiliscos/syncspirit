// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <fmt/format.h>
#include <boost/outcome.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <variant>
#include "syncspirit-export.h"
#include "bep.pb.h"
#include "utils/uri.h"

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;
namespace asio = boost::asio;

namespace payload {
using URIs = utils::uri_container_t;
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

SYNCSPIRIT_API void make_hello_message(fmt::memory_buffer &buff, std::string_view device_name) noexcept;

SYNCSPIRIT_API std::size_t make_announce_message(fmt::memory_buffer &buff, std::string_view device_name,
                                                 const payload::URIs &uris, std::int64_t instance) noexcept;

template <typename Message>
void serialize(fmt::memory_buffer &buff, const Message &message,
               proto::MessageCompression compression = proto::MessageCompression::NONE) noexcept;

SYNCSPIRIT_API outcome::result<message::wrapped_message_t> parse_bep(const asio::const_buffer &buff) noexcept;

SYNCSPIRIT_API outcome::result<message::Announce> parse_announce(const asio::const_buffer &buff) noexcept;

} // namespace syncspirit::proto
