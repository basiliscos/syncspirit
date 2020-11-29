#include "base.h"
#include "tcp.h"
#include "tls.h"

namespace syncspirit::transport {

base_t::base_t(rotor::asio::supervisor_asio_t &supervisor_) noexcept
    : supervisor{supervisor_}, strand{supervisor.get_strand()}, cancelling{false} {}

base_t::~base_t() {}

http_base_t::http_base_t(rotor::asio::supervisor_asio_t &supervisor_) noexcept : supervisor{supervisor_} {}

http_base_t::~http_base_t() {
    if (in_progess)
        std::abort();
}

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
