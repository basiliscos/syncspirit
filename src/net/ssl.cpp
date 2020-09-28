#include "ssl.h"

namespace sys = boost::system;

namespace syncspirit::net {

outcome::result<void> ssl_t::load(const char *cert, const char *priv_key) noexcept {
    auto key_pair = utils::load_pair(cert, priv_key);
    if (!key_pair) {
        return key_pair.error();
    }

    proto::device_id_t device(key_pair.value());
    device_id = std::move(device);
    pair = std::move(key_pair.value());
    return outcome::success();
}

ssl::context ssl_t::get_context() noexcept {
    ssl::context ctx(ssl::context::tls);
    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);

    auto &cert_data = pair.cert_data;
    auto &key_data = pair.key_data;
    ctx.use_certificate(asio::const_buffer(cert_data.c_str(), cert_data.size()), ssl::context::asn1);
    ctx.use_private_key(asio::const_buffer(key_data.c_str(), key_data.size()), ssl::context::asn1);

    return ctx;
}

} // namespace syncspirit::net
