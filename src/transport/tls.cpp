#include "tls.h"
#include "spdlog/spdlog.h"

using namespace syncspirit::transport;

void tls_t::async_connect(const resolved_hosts_t &hosts, const connect_fn_t &on_connect,
                          error_fn_t &on_error) noexcept {
    async_connect_impl(sock.next_layer(), hosts, on_connect, on_error);
}

void tls_t::async_handshake(const handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept {
    sock.async_handshake(ssl::stream_base::client, [&](auto ec) {
        if (ec) {
            strand.post([ec = ec, &on_error]() { on_error(ec); });
            return;
        }
        strand.post([ec = ec, &on_error]() { on_error(ec); });
    });
    strand.post([&]() { on_handshake(validation_passed); });
}

void tls_t::async_write(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept {
    async_write_impl(sock, buff, on_write, on_error);
}

void tls_t::cancel() noexcept { cancel_impl(sock.next_layer()); }

void https_t::async_read(rx_buff_t &rx_buff, response_t &response, const io_fn_t &on_read,
                         error_fn_t &on_error) noexcept {
    async_read_impl(sock, strand, rx_buff, response, on_read, on_error);
}
