#include "tls.h"
#include "spdlog/spdlog.h"

using namespace syncspirit::transport;

https_t::https_t(const transport_config_t &config) noexcept : tls_t{config}, http_base_t(config.supervisor) {}

tls_t::tls_t(const transport_config_t &config) noexcept
    : base_t(config.supervisor), expected_peer(config.ssl_junction->peer), me(*config.ssl_junction->me),
      ctx(get_context(*this)), sock{strand, ctx} {
    if (config.ssl_junction->sni_extension) {
        auto &host = config.uri.host;
        if (!SSL_set_tlsext_host_name(sock.native_handle(), host.c_str())) {
            sys::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            spdlog::error("http_actor_t:: Set SNI Hostname : {}", ec.message());
        }
    }
    auto mode = ssl::verify_peer | ssl::verify_fail_if_no_peer_cert | ssl::verify_client_once;
    sock.set_verify_depth(1);
    sock.set_verify_mode(mode);
    sock.set_verify_callback([&](bool, ssl::verify_context &peer_ctx) -> bool {
        auto native = peer_ctx.native_handle();
        auto peer_cert = X509_STORE_CTX_get_current_cert(native);
        if (!peer_cert) {
            spdlog::warn("no peer certificate");
            return false;
        }
        auto der_option = utils::as_serialized_der(peer_cert);
        if (!der_option) {
            spdlog::warn("peer certificate cannot be serialized as der : {}", der_option.error().message());
            return false;
        }

        utils::cert_data_t cert_data{std::move(der_option.value())};
        auto peer_option = model::device_id_t::from_cert(cert_data);
        if (!peer_option) {
            spdlog::warn("cannot get device_id from peer");
            return false;
        }

        auto peer = std::move(peer_option.value());
        if (!actual_peer) {
            actual_peer = peer;
            spdlog::trace("tls, peer device_id = {}", actual_peer);
        }

        if (actual_peer != expected_peer) {
            spdlog::warn("unexcpected peer device_id. Got: {}, expected: {}", actual_peer, expected_peer);
            return false;
        }
        validation_passed = true;
        return true;
    });
}

ssl::context tls_t::get_context(tls_t &source) noexcept {
    ssl::context ctx(ssl::context::tls);
    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);

    auto &cert_data = source.me.cert_data.bytes;
    auto &key_data = source.me.key_data.bytes;
    ctx.use_certificate(asio::const_buffer(cert_data.c_str(), cert_data.size()), ssl::context::asn1);
    ctx.use_private_key(asio::const_buffer(key_data.c_str(), key_data.size()), ssl::context::asn1);
    return ctx;
}

void tls_t::async_connect(const resolved_hosts_t &hosts, connect_fn_t &on_connect, error_fn_t &on_error) noexcept {
    async_connect_impl(sock.next_layer(), hosts, on_connect, on_error);
}

void tls_t::async_handshake(handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept {
    sock.async_handshake(ssl::stream_base::client, [&, on_handshake, on_error](auto ec) {
        if (ec) {
            strand.post([ec = ec, on_error, this]() {
                on_error(ec);
                supervisor.do_process();
            });
            return;
        }
        strand.post([this, on_handshake]() {
            auto peer_cert = SSL_get_peer_certificate(sock.native_handle());
            on_handshake(validation_passed, peer_cert);
            supervisor.do_process();
        });
    });
}

void tls_t::async_send(asio::const_buffer buff, const io_fn_t &on_write, error_fn_t &on_error) noexcept {
    async_send_impl(sock, buff, on_write, on_error);
}

void tls_t::async_recv(asio::mutable_buffer buff, const io_fn_t &on_read, error_fn_t &on_error) noexcept {
    async_recv_impl(sock, buff, on_read, on_error);
}

void tls_t::cancel() noexcept { cancel_impl(sock.next_layer()); }

void https_t::async_read(rx_buff_t &rx_buff, response_t &response, const io_fn_t &on_read,
                         error_fn_t &on_error) noexcept {
    async_read_impl(sock, strand, rx_buff, response, on_read, on_error);
}
