#pragma once

#include <memory>
#include <functional>
#include <rotor/asio.hpp>
#include <boost/beast.hpp>
#include "spdlog/spdlog.h"

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

struct base_t {
    virtual ~base_t();
    virtual void async_connect(const resolved_hosts_t &hosts, const connect_fn_t &on_connect,
                               error_fn_t &on_error) noexcept = 0;
    virtual void async_handshake(const handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept = 0;
    virtual void async_write(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept = 0;
    virtual void cancel() noexcept = 0;

  protected:
    strand_t &strand;

    template <typename Socket>
    void async_connect_impl(Socket &sock, const resolved_hosts_t &hosts, const connect_fn_t &on_connect,
                            error_fn_t &on_error) noexcept {
        asio::async_connect(sock, hosts.begin(), hosts.end(), [&](auto &ec, auto addr) {
            if (ec) {
                strand.post([ec = ec, &on_error]() { on_error(ec); });
                return;
            }
            strand.post([addr = addr, &on_connect]() { on_connect(addr); });
        });
    }

    template <typename Socket>
    void async_write_impl(Socket &sock, asio::const_buffer buff, const io_fn_t &on_write,
                          error_fn_t &on_error) noexcept {
        asio::async_write(sock, buff, [&](auto &ec, auto bytes) {
            if (ec) {
                strand.post([ec = ec, &on_error]() { on_error(ec); });
                return;
            }
            strand.post([bytes = bytes, &on_write]() { on_write(bytes); });
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
        http::async_read(sock, rx_buff, response, [&](auto ec, auto bytes) {
            if (ec) {
                strand.post([ec = ec, &on_error]() { on_error(ec); });
                return;
            }
            strand.post([bytes = bytes, &on_read]() { on_read(bytes); });
        });
    }
};

using transport_sp_t = std::unique_ptr<base_t>;

} // namespace syncspirit::transport
