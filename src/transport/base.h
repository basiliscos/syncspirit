#pragma once

#include <memory>
#include <functional>
#include <optional>
#include <memory>
#include <rotor/asio.hpp>
#include "../model/device_id.h"
#include "../model/arc.hpp"
#include "../utils/tls.h"
#include "../utils/uri.h"

namespace syncspirit::transport {

namespace asio = boost::asio;
namespace sys = boost::system;

using tcp = asio::ip::tcp;

using strand_t = asio::io_context::strand;
using resolver_t = tcp::resolver;
using resolved_hosts_t = resolver_t::results_type;
using resolved_item_t = resolved_hosts_t::iterator;

using connect_fn_t = std::function<void(resolved_item_t)>;
using error_fn_t = std::function<void(const sys::error_code &)>;
using handshake_fn_t =
    std::function<void(bool valid, X509 *peer, const tcp::endpoint &, const model::device_id_t *peer_device)>;
using io_fn_t = std::function<void(std::size_t)>;

struct ssl_junction_t {
    model::device_id_t peer;
    const utils::key_pair_t *me;
    bool sni_extension;
    std::string_view alpn = ""; /* application layer protocol names? */
};

using ssl_option_t = std::optional<ssl_junction_t>;

struct transport_config_t {
    ssl_option_t ssl_junction;
    utils::URI uri;
    rotor::asio::supervisor_asio_t &supervisor;
    std::optional<tcp::socket> sock;
};

} // namespace syncspirit::transport
