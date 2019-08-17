#pragma once

#include "../configuration.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace ra = rotor::asio;
namespace asio = boost::asio;

struct net_supervisor_t : public ra::supervisor_asio_t {
    net_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx, const ra::supervisor_config_t &sup_cfg,
                     const config::configuration_t &cfg);

    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void confirm_shutdown() noexcept override;

  private:
    using guard_t = asio::executor_work_guard<asio::io_context::executor_type>;

    void launch_discovery() noexcept;
    void launch_upnp() noexcept;

    config::configuration_t cfg;
    guard_t guard;
};

} // namespace net
} // namespace syncspirit
