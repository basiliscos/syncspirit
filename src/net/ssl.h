#pragma once

#include <boost/asio/ssl.hpp>
#include <boost/outcome.hpp>
#include "../proto/device_id.h"
#include "../utils/tls.h"

namespace syncspirit {
namespace net {

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace outcome = boost::outcome_v2;

struct ssl_t {
    outcome::result<void> load(const char *cert, const char *priv_key) noexcept;

    ssl::context get_context() const noexcept;

  private:
    proto::device_id_t device_id;
    utils::key_pair_t pair;
};

struct ssl_context_t {
    using verify_callback_t = std::function<bool(bool preverified, ssl::verify_context &peer_ctx)>;
    ssl::context ctx;
    int verify_depth;
    ssl::verify_mode verify_mode;
    verify_callback_t verify_callback;
};

ssl_context_t make_context(const ssl_t &ssl, const proto::device_id_t &device_id) noexcept;

} // namespace net
} // namespace syncspirit
