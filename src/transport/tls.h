#pragma once

#include "base.h"
#include <boost/asio/ssl.hpp>

namespace syncspirit::transport {

struct tls_t : base_t {
    using socket_t = ssl::stream<tcp::socket>;

    tls_t(transport_config_t &config) noexcept;
    void async_connect(const resolved_hosts_t &hosts, connect_fn_t &on_connect, error_fn_t &on_error) noexcept override;
    void async_handshake(handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept override;
    void async_send(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept override;
    void async_recv(asio::mutable_buffer buff, const io_fn_t &on_read, error_fn_t &on_error) noexcept override;
    void cancel() noexcept override;
    asio::ip::address local_address(sys::error_code &ec) noexcept override;

  private:
    ssl::context get_context(tls_t &me) noexcept;

  protected:
    model::device_id_t expected_peer;
    const utils::key_pair_t &me;
    ssl::context ctx;
    ssl::stream_base::handshake_type role;
    socket_t sock;
    bool validation_passed = false;
};

struct https_t : tls_t, http_base_t {
    https_t(transport_config_t &config) noexcept;

    void async_read(rx_buff_t &rx_buff, response_t &response, const io_fn_t &on_read,
                    error_fn_t &on_error) noexcept override;
};

} // namespace syncspirit::transport
