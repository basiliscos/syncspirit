#include "ssl.h"
#include "spdlog/spdlog.h"

namespace sys = boost::system;

namespace syncspirit::net {

outcome::result<void> ssl_t::load(const char *cert, const char *priv_key) noexcept {
    auto key_pair = utils::load_pair(cert, priv_key);
    if (!key_pair) {
        return key_pair.error();
    }

    proto::device_id_t device(key_pair.value().cert_data);
    device_id = std::move(device);
    pair = std::move(key_pair.value());
    return outcome::success();
}

ssl::context ssl_t::get_context() const noexcept {
    ssl::context ctx(ssl::context::tls);
    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);

    auto &cert_data = pair.cert_data.bytes;
    auto &key_data = pair.key_data.bytes;
    ctx.use_certificate(asio::const_buffer(cert_data.c_str(), cert_data.size()), ssl::context::asn1);
    ctx.use_private_key(asio::const_buffer(key_data.c_str(), key_data.size()), ssl::context::asn1);

    return ctx;
}

ssl_context_t make_context(const ssl_t &ssl, const proto::device_id_t &device_id) noexcept {
    auto ctx = ssl.get_context();
    int depth = 1;
    auto mode = ssl::verify_peer | ssl::verify_fail_if_no_peer_cert | ssl::verify_client_once;
    ssl_context_t::verify_callback_t cb = [&device_id](bool, ssl::verify_context &peer_ctx) -> bool {
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
        auto peer_device_id = proto::device_id_t(cert_data);
        spdlog::debug("peer device_id = {}", peer_device_id.value);
        if (peer_device_id != device_id) {
            spdlog::warn("unexcpected peer device_id. Got: {}, expected: {}", peer_device_id.value, device_id.value);
            return false;
        }
        return true;
    };
    return ssl_context_t{std::move(ctx), depth, mode, std::move(cb)};
}

} // namespace syncspirit::net
