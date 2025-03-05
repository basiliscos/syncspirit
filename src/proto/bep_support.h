// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <fmt/format.h>
#include <boost/outcome.hpp>
#include <variant>
#include "syncspirit-export.h"
#include "proto/proto-fwd.hpp"
#include "utils/uri.h"
#include "utils/bytes.h"

namespace syncspirit::proto {

namespace outcome = boost::outcome_v2;

namespace payload {
using URIs = utils::uri_container_t;
}

namespace message {

using message_t =
    std::variant<Hello, ClusterConfig, Index, IndexUpdate, Request, Response, DownloadProgress, Ping, Close>;

struct wrapped_message_t {
    message_t message;
    std::size_t consumed = 0;
};

template <typename T>
consteval MessageType get_bep_type() {
    if constexpr (std::is_same_v<T, ClusterConfig>)  {
        return MessageType::CLUSTER_CONFIG;
    } else if constexpr (std::is_same_v<T, Index>)  {
        return MessageType::INDEX;
    } else if constexpr (std::is_same_v<T, IndexUpdate>)  {
        return MessageType::INDEX_UPDATE;
    } else if constexpr (std::is_same_v<T, Request>)  {
        return MessageType::REQUEST;
    } else if constexpr (std::is_same_v<T, Response>)  {
        return MessageType::RESPONSE;
    } else if constexpr (std::is_same_v<T, DownloadProgress>)  {
        return MessageType::DOWNLOAD_PROGRESS;
    } else if constexpr (std::is_same_v<T, Ping>)  {
        return MessageType::PING;
    } else if constexpr (std::is_same_v<T, Close>)  {
        return MessageType::CLOSE;
    } else if constexpr (std::is_same_v<T, Hello>)  {
        return MessageType::HELLO;
    }
}

} // namespace message

SYNCSPIRIT_API utils::bytes_t make_hello_message(std::string_view device_name) noexcept;

SYNCSPIRIT_API std::size_t make_announce_message(utils::bytes_view_t storage, utils::bytes_view_t device_id,
                                                 const payload::URIs &uris, std::int64_t instance) noexcept;

template <typename Message>
utils::bytes_t serialize(const Message &message,
               proto::MessageCompression compression = proto::MessageCompression::NONE) noexcept;

SYNCSPIRIT_API outcome::result<message::wrapped_message_t> parse_bep(utils::bytes_view_t) noexcept;

SYNCSPIRIT_API outcome::result<Announce> parse_announce(utils::bytes_view_t) noexcept;
} // namespace syncspirit::proto
