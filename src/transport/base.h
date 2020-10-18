#pragma once

#include <memory>
#include <functional>
#include <optional>
#include <memory>
#include <rotor/asio.hpp>
#include <boost/beast.hpp>
#include "spdlog/spdlog.h"
#include "../model/device_id.h"
#include "../utils/tls.h"
#include "../utils/uri.h"

namespace syncspirit::transport {

namespace asio = boost::asio;
namespace sys = boost::system;
namespace http = boost::beast::http;
namespace ssl = asio::ssl;

using tcp = asio::ip::tcp;

using strand_t = asio::io_context::strand;
using resolver_t = tcp::resolver;
using resolved_hosts_t = resolver_t::results_type;
using resolved_item_t = resolved_hosts_t::iterator;

using connect_fn_t = std::function<void(resolved_item_t)>;
using error_fn_t = std::function<void(const sys::error_code &)>;
using handshake_fn_t = std::function<void(bool valid)>;
using io_fn_t = std::function<void(std::size_t)>;

struct ssl_junction_t {
    model::device_id_t peer;
    const utils::key_pair_t *me;
    bool sni_extension;
};

using ssl_option_t = std::optional<ssl_junction_t>;

struct transport_config_t {
    ssl_option_t ssl_junction;
    utils::URI uri;
    strand_t &strand;
};

struct base_t {
    virtual ~base_t();
    virtual void async_connect(const resolved_hosts_t &hosts, connect_fn_t &on_connect,
                               error_fn_t &on_error) noexcept = 0;
    virtual void async_handshake(handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept = 0;
    virtual void async_write(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept = 0;
    virtual void cancel() noexcept = 0;

    const model::device_id_t &peer_identity() noexcept { return actual_peer; }

  protected:
    base_t(strand_t &strand_) noexcept;
    strand_t &strand;
    model::device_id_t actual_peer;

    template <typename Socket>
    void async_connect_impl(Socket &sock, const resolved_hosts_t &hosts, connect_fn_t &on_connect,
                            error_fn_t &on_error) noexcept {
        asio::async_connect(sock, hosts.begin(), hosts.end(), [&, on_connect, on_error](auto &ec, auto addr) {
            if (ec) {
                strand.post([ec = ec, on_error]() { on_error(ec); });
                return;
            }
            strand.post([addr = addr, on_connect]() { on_connect(addr); });
        });
    }

    template <typename Socket>
    void async_write_impl(Socket &sock, asio::const_buffer buff, const io_fn_t &on_write,
                          error_fn_t &on_error) noexcept {
        asio::async_write(sock, buff, [&, on_write, on_error](auto &ec, auto bytes) {
            if (ec) {
                strand.post([ec = ec, on_error]() { on_error(ec); });
                return;
            }
            strand.post([bytes = bytes, on_write]() { on_write(bytes); });
        });
    }

    template <typename Socket> void cancel_impl(Socket &sock) noexcept {
        sys::error_code ec;
        sock.cancel(ec);
        if (ec) {
            spdlog::error("base_t::cancel() :: {}", ec.message());
        }
    }
};

using transport_sp_t = std::unique_ptr<base_t>;
transport_sp_t initiate(const transport_config_t &config) noexcept;

struct http_base_t {
    using rx_buff_t = boost::beast::flat_buffer;
    using response_t = http::response<http::string_body>;

    virtual ~http_base_t();
    virtual void async_read(rx_buff_t &rx_buff, response_t &response, const io_fn_t &on_read,
                            error_fn_t &on_error) noexcept = 0;

  protected:
    template <typename Socket>
    void async_read_impl(Socket &sock, strand_t &strand, rx_buff_t &rx_buff, response_t &response,
                         const io_fn_t &on_read, error_fn_t &on_error) noexcept {
        http::async_read(sock, rx_buff, response, [&, on_read, on_error](auto ec, auto bytes) {
            if (ec) {
                strand.post([ec = ec, on_error]() { on_error(ec); });
                return;
            }
            strand.post([bytes = bytes, on_read]() { on_read(bytes); });
        });
    }
};

} // namespace syncspirit::transport
