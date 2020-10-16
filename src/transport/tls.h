#pragma once

#include "base.h"
#include <boost/asio/ssl.hpp>

namespace syncspirit::transport {

struct tls_t : base_t {
    void async_connect(const resolved_hosts_t &hosts, const connect_fn_t &on_connect,
                       error_fn_t &on_error) noexcept override;
    void async_handshake(const handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept override;
    void async_write(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept override;
    void cancel() noexcept override;

  protected:
    using socket_t = ssl::stream<tcp::socket>;
    strand_t &strand;
    socket_t sock;
    bool validation_passed = false;
};

struct https_t : tls_t, http_base_t {
    using tls_t::tls_t;

    void async_read(rx_buff_t &rx_buff, response_t &response, const io_fn_t &on_read,
                    error_fn_t &on_error) noexcept override;
};

} // namespace syncspirit::transport
