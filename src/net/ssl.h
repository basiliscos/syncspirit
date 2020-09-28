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

    ssl::context get_context() noexcept;

  private:
    proto::device_id_t device_id;
    utils::key_pair_t pair;
};

} // namespace net
} // namespace syncspirit
