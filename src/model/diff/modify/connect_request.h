#pragma once

#include <rotor/asio.hpp>
#include <mutex>
#include "../contact_diff.h"
#include "model/cluster.h"

namespace syncspirit::model::diff::modify {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct connect_request_t final : contact_diff_t {
    using socket_ptr_t = std::unique_ptr<tcp::socket>;
    using mutex_t = std::mutex;

    connect_request_t(tcp::socket sock, const tcp::endpoint &remote) noexcept;

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(contact_visitor_t &) const noexcept override;

    tcp::endpoint remote;
    mutable mutex_t mutex;
    mutable socket_ptr_t sock;
};

} // namespace syncspirit::model::diff::modify
