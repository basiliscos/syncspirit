#include "base.h"
#include "tcp.h"
#include "tls.h"

namespace syncspirit::transport {

base_t::base_t(strand_t &strand_) noexcept : strand{strand_} {}

base_t::~base_t() {}

http_base_t::~http_base_t() {}

transport_sp_t initiate(const transport_config_t &config) noexcept {
    auto &proto = config.uri.proto;
    if (!config.ssl_junction) {
        if (proto == "http") {
            return std::make_unique<http_t>(config);
        }
    } else {
        if (proto == "tcp") {
            return std::make_unique<tls_t>(config);
        } else if (proto == "https") {
            return std::make_unique<https_t>(config);
        }
    }
    return transport_sp_t();
}

} // namespace syncspirit::transport
