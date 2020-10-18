#include "tcp.h"
#include "spdlog/spdlog.h"

using namespace syncspirit::transport;

tcp_t::tcp_t(const transport_config_t &config) noexcept : base_t{config.strand}, sock(strand) {
    assert(!config.ssl_junction);
}

void tcp_t::async_connect(const resolved_hosts_t &hosts, connect_fn_t &on_connect, error_fn_t &on_error) noexcept {
    async_connect_impl(sock, hosts, on_connect, on_error);
}

void tcp_t::async_handshake(handshake_fn_t &on_handshake, error_fn_t &) noexcept {
    strand.post([on_handshake]() { on_handshake(true); });
}

void tcp_t::async_write(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept {
    async_write_impl(sock, buff, on_write, on_error);
}

void tcp_t::cancel() noexcept { cancel_impl(sock); }

void http_t::async_read(rx_buff_t &rx_buff, response_t &response, const io_fn_t &on_read,
                        error_fn_t &on_error) noexcept {
    async_read_impl(sock, strand, rx_buff, response, on_read, on_error);
}
