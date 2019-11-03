#pragma once

#include "../configuration.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

struct net_supervisor_t : public ra::supervisor_asio_t {
    net_supervisor_t(ra::supervisor_asio_t *sup, const ra::supervisor_config_asio_t &sup_cfg,
                     const config::configuration_t &cfg);

    /*
    virtual void on_initialize(r::message::init_request_t &) noexcept override;
    virtual void on_initialize_confirm(r::message::init_response_t &msg) noexcept override;
    */
    virtual void init_start() noexcept override;
    virtual void on_registration(r::message::registration_response_t &) noexcept;
    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_shutdown(r::message::shutdown_request_t &) noexcept override;
    virtual void shutdown_finish() noexcept override;

  private:
    using guard_t = asio::executor_work_guard<asio::io_context::executor_type>;

    void launch_discovery() noexcept;
    void launch_upnp() noexcept;
    void launch_acceptor() noexcept;

    config::configuration_t cfg;
    guard_t guard;
    r::address_ptr_t registry_addr;
};

} // namespace net
} // namespace syncspirit
