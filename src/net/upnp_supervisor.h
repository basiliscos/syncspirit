#pragma once

#include "messages.h"
#include "../configuration.h"
#include "rotor/asio/supervisor_asio.h"

namespace syncspirit {
namespace net {

namespace ra = rotor::asio;
namespace r = rotor;

class upnp_supervisor_t : public ra::supervisor_asio_t {
  public:
    upnp_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx, const ra::supervisor_config_t &sup_cfg,
                      const config::upnp_config_t &cfg);

    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_shutdown(r::message_t<r::payload::shutdown_request_t> &) noexcept override;

  private:
    r::address_ptr_t http_addr;
    config::upnp_config_t cfg;
};

} // namespace net
} // namespace syncspirit
