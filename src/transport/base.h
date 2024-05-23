// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <memory>
#include <functional>
#include <optional>
#include <memory>
#include <rotor/asio.hpp>
#include "model/device_id.h"
#include "model/misc/arc.hpp"
#include "utils/tls.h"
#include "utils/uri.h"
#include "syncspirit-export.h"

namespace syncspirit::transport {

namespace asio = boost::asio;
namespace sys = boost::system;
namespace ra = rotor::asio;

using tcp = asio::ip::tcp;

using strand_t = asio::io_context::strand;
using resolver_t = tcp::resolver;
using resolved_hosts_t = resolver_t::results_type;

using connect_fn_t = std::function<void(const tcp::endpoint &)>;
using error_fn_t = std::function<void(const sys::error_code &)>;
using handshake_fn_t =
    std::function<void(bool valid, utils::x509_t &peer, const tcp::endpoint &, const model::device_id_t *peer_device)>;
using io_fn_t = std::function<void(std::size_t)>;

struct ssl_junction_t {
    model::device_id_t peer;
    const utils::key_pair_t *me;
    bool sni_extension;
    std::string_view alpn; /* application layer protocol names? */
};

using ssl_option_t = std::optional<ssl_junction_t>;

struct transport_config_t {
    ssl_option_t ssl_junction;
    utils::uri_ptr_t uri;
    ra::supervisor_asio_t &supervisor;
    std::optional<tcp::socket> sock;
    bool active;
};

struct stream_base_t;

using stream_sp_t = model::intrusive_ptr_t<stream_base_t>;

} // namespace syncspirit::transport
